#include "driver2.h"
#include "sky.h"
#include "system.h"
#include "mission.h"
#include "models.h"
#include "draw.h"
#include "camera.h"
#include "main.h"
#include "debris.h"
#include "players.h"
#include "draw.h"

#ifndef PSX
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "PsyX/PsyX_render.h"
#endif

#ifdef PSX
#define SkyOvercast()	(0)
#else
// fog and snow (draw.c) use the grey rain sky
extern int gFogTarget;
extern int gSnowTarget;
#define SkyOvercast()	(gFogTarget || gSnowTarget)
#endif

struct RGB16
{
	short r, g, b, pad;
};

struct FLAREREC
{
	RGB16 transparency;
	char size;
	short gapmod;
};

int sky_y_offset[4] = { -17, -17, -17, -17 };

unsigned char HorizonLookup[4][4] = {
	{0, 0, 20, 20},
	{20, 20, 0, 0},
	{0, 0, 20, 20},
	{0, 0, 20, 20},
};

unsigned char HorizonTextures[40] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
	0, 1, 2, 3, 4, 5, 6, 7,
	20, 21, 22, 23, 24, 25, 26, 27,
	16, 17, 18, 19
};


SVECTOR sun_position[4] = {
	{-32000,-11000,-5000},
	{-32000,-11000,-5000},
	{-32000,-11000,-5000},
	{-32000,-11000,-5000}
};

SVECTOR moon_position[4] = {
	{-32000,-11500,-18000},
	{-32000,-11500,-18000,},
	{-32000,-11500,-18000,},
	{-32000,-11500,-18000,}
};

SVECTOR moon_shadow_position[4] =
{
	{ 33536, 54036, 47536, 0 },
	{ 33536, 54036, 47536, 0 },
	{ 33536, 54036, 47536, 0 },
	{ 33536, 10500, 47536, 0 }
};

FLAREREC flare_info[8] =
{
	{
		{0,0,90},16, 48
	},
	{
		{80,40,4},48,72
	},
	{
		{90,0,0},48,96
	},
	{
		{0,90,0},16,144
	},
	{
		{0,0,90},64,160
	},
	{
		{90,90,0},32,184
	},
	{
		{90,0,0},32,208
	},
	{
		{0,0,90},48,15
	}
};

int tunnelDir[3][2] =
{
	{2000, 3064},
	{2048, 0},
	{2048, 4095}
};

VECTOR tunnelPos[3][2] =
{
	{
		{ -398650, 0, -161000, 0 },
		{ -340300, 2000, -103000, 0 }
	},
	{
		{ 272800, 0, 41200, 0 },
		{ 271963, 2000, 168187, 0 }
	},

	{
		{ -115100, 0, -193200, 0 },
		{ -113300, -400, -132501, 0 }
	}
};

UV skytexuv[28] = { 0 };
short skyclut[28];
short skytpage[28];

#ifndef PSX
// HD sky: the 512x252 sky image (4 bit, 12 tiles of 128x84, one CLUT per tile) is decoded to RGBA and upscaled
// 4x with a Catmull-Rom filter at load time, from the player's own SKYn.RAW. It is bound as an override texture
// (DR_PSYX_TEX) for the sky quads only. The quads keep their PSX texture coordinates: the image is split into two
// 256 pixel wide halves (a byte holds u), each sampled as a 256x256 virtual texture.
#define SKY_HD_SCALE	4
#define SKY_TILE_W		128
#define SKY_TILE_H		84
#define SKY_SRC_W		512
#define SKY_SRC_H		252
#define SKY_PANO_H		168		// rows 0..167 are one panorama (2 tile rows), rows 168..251 another one
#define SKY_HD_W		(SKY_SRC_W * SKY_HD_SCALE)
#define SKY_HD_HALF_W	(256 * SKY_HD_SCALE)
#define SKY_HD_TEX_H	(256 * SKY_HD_SCALE)

int gSkyHd = 2;							// config.ini [render] skyHd: 0 original, 1 smooth upscale, 2 plus denoise
static TextureID skyHdTexture[2] = { 0, 0 };

typedef struct
{
	int tap[4];
	float w[4];
} SKYTAP;

static float CatmullRom(float x)
{
	x = fabsf(x);

	if (x < 1.0f)
		return (1.5f * x - 2.5f) * x * x + 1.0f;

	if (x < 2.0f)
		return ((-0.5f * x + 2.5f) * x - 4.0f) * x + 2.0f;

	return 0.0f;
}

// taps of upscaled pixel o, clamped to the source range lo..hi-1
static void SkyHdTaps(SKYTAP* t, int o, int lo, int hi)
{
	float s = (o + 0.5f) / SKY_HD_SCALE - 0.5f;
	int b = (int)floorf(s);
	float f = s - b;
	int i;

	for (i = 0; i < 4; i++)
	{
		int p = b - 1 + i;
		t->tap[i] = p < lo ? lo : (p >= hi ? hi - 1 : p);
		t->w[i] = CatmullRom(f - (i - 1));
	}
}

// every tile has its own CLUT: along a tile border the colour can step. Per line the median step across the border
// (minus the natural step at its sides, so real edges stay) is spread out over SKY_SEAM_FEATHER pixels on both sides.
#define SKY_SEAM_FEATHER	24
#define SKY_SEAM_MEDIAN		21

static float SkyMedian(float* v, int n)
{
	int i, j;

	for (i = 1; i < n; i++)
	{
		float k = v[i];

		for (j = i - 1; j >= 0 && v[j] > k; j--)
			v[j + 1] = v[j];

		v[j + 1] = k;
	}

	return v[n / 2];
}

// img: lines x positions of RGB; the border lies between positions b-1 and b, positions run up to hi
static void SkyHdSeam(unsigned char* img, int lines, int lineStride, int posStride, int b, int hi)
{
	static float step[SKY_SRC_W][3];
	float win[SKY_SEAM_MEDIAN];
	int l, c, k, m;

#define SKY_PX(o)	((float)p[(b + (o)) * posStride + c])
	for (l = 0; l < lines; l++)
	{
		const unsigned char* p = img + l * lineStride;

		for (c = 0; c < 3; c++)
		{
			float at = (SKY_PX(0) + SKY_PX(1)) - (SKY_PX(-1) + SKY_PX(-2));
			float lft = (SKY_PX(-3) + SKY_PX(-4)) - (SKY_PX(-5) + SKY_PX(-6));
			float rgt = (SKY_PX(5) + SKY_PX(6)) - (SKY_PX(3) + SKY_PX(4));

			step[l][c] = (at - 0.5f * (lft + rgt)) * 0.5f;
		}
	}
#undef SKY_PX

	for (l = 0; l < lines; l++)
	{
		unsigned char* p = img + l * lineStride;

		for (c = 0; c < 3; c++)
		{
			float ex;
			int n = 0;

			for (m = l - SKY_SEAM_MEDIAN / 2; m <= l + SKY_SEAM_MEDIAN / 2; m++)
			{
				if (m >= 0 && m < lines)
					win[n++] = step[m][c];
			}

			ex = SkyMedian(win, n) * 0.5f;

			for (k = 0; k < SKY_SEAM_FEATHER; k++)
			{
				float w = ex * (1.0f - (k + 0.5f) / SKY_SEAM_FEATHER);
				unsigned char* r = &p[(b + k) * posStride + c];
				unsigned char* q = &p[(b - 1 - k) * posStride + c];
				float v;

				if (b + k < hi)
				{
					v = *r - w + 0.5f;
					*r = (unsigned char)(v < 0.0f ? 0 : (v > 255.0f ? 255 : v));
				}

				if (b - 1 - k >= 0)
				{
					v = *q + w + 0.5f;
					*q = (unsigned char)(v < 0.0f ? 0 : (v > 255.0f ? 255 : v));
				}
			}
		}
	}
}

// edge preserving smoothing (bilateral, 5x5, over the tile borders, inside one panorama) of the 4 bit dither and palette banding
static void SkyHdDenoise(unsigned char src[SKY_SRC_H][SKY_SRC_W][3])
{
	static unsigned char dst[SKY_SRC_H][SKY_SRC_W][3];
	int x, y, dx, dy, c;

	for (y = 0; y < SKY_SRC_H; y++)
	{
		int ylo = y < SKY_PANO_H ? 0 : SKY_PANO_H;
		int yhi = y < SKY_PANO_H ? SKY_PANO_H : SKY_SRC_H;

		for (x = 0; x < SKY_SRC_W; x++)
		{
			float sum[3] = { 0, 0, 0 };
			float wsum = 0.0f;

			for (dy = -2; dy <= 2; dy++)
			{
				int yy = y + dy;

				if (yy < ylo || yy >= yhi)
					continue;

				for (dx = -2; dx <= 2; dx++)
				{
					int xx = x + dx;
					float d2 = 0.0f, w;

					if (xx < 0 || xx >= SKY_SRC_W)
						continue;

					for (c = 0; c < 3; c++)
					{
						float e = (float)src[yy][xx][c] - (float)src[y][x][c];
						d2 += e * e;
					}

					w = expf(-(dx * dx + dy * dy) / 4.5f - d2 / 1568.0f);	// sigma 1.5 px, 28 levels

					for (c = 0; c < 3; c++)
						sum[c] += w * src[yy][xx][c];

					wsum += w;
				}
			}

			for (c = 0; c < 3; c++)
				dst[y][x][c] = (unsigned char)(sum[c] / wsum + 0.5f);
		}
	}

	memcpy(src, dst, sizeof(dst));
}

// blk = the 128 x 256 halfword sky block as it goes to VRAM (x 320, y 0): 4 bit pixels, CLUTs in rows 252..254
static void SkyHdBuild(const unsigned short* blk, TextureID* dst)
{
	static unsigned char src[SKY_SRC_H][SKY_SRC_W][3];
	static SKYTAP tapX[SKY_HD_W];
	static SKYTAP tapY[SKY_SRC_H * SKY_HD_SCALE];

	float* tmp;
	unsigned char* out[2];
	int h, i, x, y, ry, tx, n, c;
	int oldFilter;

	for (h = 0; h < 2; h++)
	{
		if (dst[h])
			GR_DestroyTexture(dst[h]);

		dst[h] = 0;
	}

	if (!gSkyHd)
		return;

	// 4 bit pixels with the CLUT of their tile
	for (ry = 0; ry < 3; ry++)
	{
		for (tx = 0; tx < 4; tx++)
		{
			unsigned char pal[16][3];
			const unsigned short* clut = blk + (252 + ry) * 128 + tx * 16;

			for (i = 0; i < 16; i++)
			{
				// same expansion as the PsyCross palette lookup (5 bit << 3), so the colours match the PSX path
				pal[i][0] = (unsigned char)((clut[i] & 31) << 3);
				pal[i][1] = (unsigned char)(((clut[i] >> 5) & 31) << 3);
				pal[i][2] = (unsigned char)(((clut[i] >> 10) & 31) << 3);
			}

			for (y = 0; y < SKY_TILE_H; y++)
			{
				const unsigned short* row = blk + (ry * SKY_TILE_H + y) * 128 + tx * 32;

				for (x = 0; x < SKY_TILE_W; x++)
				{
					int idx = (row[x >> 2] >> ((x & 3) * 4)) & 15;
					memcpy(src[ry * SKY_TILE_H + y][tx * SKY_TILE_W + x], pal[idx], 3);
				}
			}
		}
	}

	for (i = 1; i < 4; i++)
		SkyHdSeam(&src[0][0][0], SKY_SRC_H, SKY_SRC_W * 3, 3, i * SKY_TILE_W, SKY_SRC_W);

	SkyHdSeam(&src[0][0][0], SKY_SRC_W, 3, SKY_SRC_W * 3, SKY_TILE_H, SKY_PANO_H);

	if (gSkyHd >= 2)
		SkyHdDenoise(src);

	for (i = 0; i < SKY_HD_W; i++)
		SkyHdTaps(&tapX[i], i, 0, SKY_SRC_W);

	for (i = 0; i < SKY_SRC_H * SKY_HD_SCALE; i++)
		SkyHdTaps(&tapY[i], i, i < SKY_PANO_H * SKY_HD_SCALE ? 0 : SKY_PANO_H, i < SKY_PANO_H * SKY_HD_SCALE ? SKY_PANO_H : SKY_SRC_H);

	tmp = (float*)malloc(sizeof(float) * 3 * SKY_SRC_H * SKY_HD_W);
	out[0] = (unsigned char*)malloc(4 * SKY_HD_HALF_W * SKY_HD_TEX_H);
	out[1] = (unsigned char*)malloc(4 * SKY_HD_HALF_W * SKY_HD_TEX_H);

	if (!tmp || !out[0] || !out[1])
	{
		free(tmp);
		free(out[0]);
		free(out[1]);
		return;
	}

	// horizontal pass
	for (y = 0; y < SKY_SRC_H; y++)
	{
		for (x = 0; x < SKY_HD_W; x++)
		{
			const SKYTAP* t = &tapX[x];

			for (c = 0; c < 3; c++)
			{
				float v = 0.0f;

				for (n = 0; n < 4; n++)
					v += t->w[n] * src[y][t->tap[n]][c];

				tmp[(y * SKY_HD_W + x) * 3 + c] = v;
			}
		}
	}

	// vertical pass, rows below the image (256 - 252 source rows) repeat the last row
	for (y = 0; y < SKY_HD_TEX_H; y++)
	{
		int yy = y < SKY_SRC_H * SKY_HD_SCALE ? y : SKY_SRC_H * SKY_HD_SCALE - 1;
		const SKYTAP* t = &tapY[yy];

		for (x = 0; x < SKY_HD_W; x++)
		{
			unsigned char* px = out[x / SKY_HD_HALF_W] + (y * SKY_HD_HALF_W + (x % SKY_HD_HALF_W)) * 4;

			for (c = 0; c < 3; c++)
			{
				float v = 0.5f;

				for (n = 0; n < 4; n++)
					v += t->w[n] * tmp[(t->tap[n] * SKY_HD_W + x) * 3 + c];

				px[c] = v < 0.0f ? 0 : (v > 255.0f ? 255 : (unsigned char)v);
			}

			px[3] = 255;
		}
	}

	// the texture filter is chosen when the texture is created: the HD sky is always smooth
	oldFilter = g_cfg_bilinearFiltering;
	g_cfg_bilinearFiltering = 1;

	for (h = 0; h < 2; h++)
		dst[h] = GR_CreateRGBATexture(SKY_HD_HALF_W, SKY_HD_TEX_H, out[h]);

	g_cfg_bilinearFiltering = oldFilter;

	free(tmp);
	free(out[0]);
	free(out[1]);
}
#endif

static int SkyNum(void)
{
	return (BASE_CITY >= 1 && BASE_CITY <= 3) ? BASE_CITY : 0;
}

// segment of SKY<n>.RAW (64 kB each) for a time of day: 0 day, 1 night, 2 overcast, 3 dawn, 4 dusk
static int SkySegment(int timeOfDay)
{
	if (gWeather - 1U < 2 || SkyOvercast())
		return timeOfDay == TIME_NIGHT ? 1 : 2;

	if (timeOfDay == TIME_DAWN)
		return 3;
	if (timeOfDay == TIME_DUSK)
		return 4;
	if (timeOfDay == TIME_NIGHT)
		return 1;

	return 0;
}

#ifndef PSX
#define SKY_SEGMENTS	5

static TextureID skyCycleTexture[SKY_SEGMENTS][2];
static int skyCycleBuilt = 0;
static int skyCycleFrom = -1, skyCycleTo = -1;
static float skyCycleT = 0.0f;
static int skyCycleLight[2];

static TextureID* skyDrawTexture = skyHdTexture;	// HD texture pair PlotSkyPoly binds
static int skyDrawAdd = 0;							// 1: PlotSkyPoly adds its quads to the sky drawn before (cross fade)

static void SkyCycleRelease(void)
{
	int s, h;

	for (s = 0; s < SKY_SEGMENTS; s++)
	{
		for (h = 0; h < 2; h++)
		{
			if (skyCycleTexture[s][h])
				GR_DestroyTexture(skyCycleTexture[s][h]);

			skyCycleTexture[s][h] = 0;
		}
	}

	skyCycleBuilt = 0;
}

// builds the HD sky of every time of day once per level (about as long as one LoadSky each)
static void SkyCycleBuild(void)
{
	char name[16];
	unsigned short* blk;
	int time, segment;

	skyCycleBuilt = 1;

	if (!gSkyHd)
		return;

	blk = (unsigned short*)malloc(0x10000);
	sprintf(name, "DATA\\SKY%d.RAW", SkyNum());

	for (time = TIME_DAWN; time <= TIME_NIGHT; time++)
	{
		segment = SkySegment(time);

		if (skyCycleTexture[segment][0])
			continue;

		LoadfileSeg(name, (char*)blk, segment << 16, 0x10000);
		SkyHdBuild(blk, skyCycleTexture[segment]);
	}

	free(blk);
}

void SkyCycleSet(int from, int to, float t, int lightFrom, int lightTo)
{
	skyCycleFrom = from;
	skyCycleTo = to;
	skyCycleT = t;
	skyCycleLight[0] = lightFrom;
	skyCycleLight[1] = lightTo;
}

int SkyCycleReady(void)
{
	return skyCycleBuilt && skyCycleTexture[SkySegment(TIME_DAY)][0] != 0;
}
#else
static const int skyCycleFrom = -1;

void SkyCycleSet(int from, int to, float t, int lightFrom, int lightTo) {}
int SkyCycleReady(void) { return 0; }
#endif

// [D] [T]
void LoadSky(void)
{
	char name[16];

	int skyNum;
	RECT16 rect;
	
	int offset;

	int flipped;
	int single;

	int u,v,x,y,ry;
	int tp_x, clut_x, i;

	i = 0;
	y = 0;
	offset = 0;
	flipped = 1;

	do {
		if (flipped) 
		{
			switch(y)
			{
				default:
					ry = 0;
					flipped = 0;
					single = 1;
					break;
				case 1:
					ry = 0;
					flipped = 0;
					single = 0;
					break;
				case 2:
					ry = 1;
					flipped = 0;
					single = 0;
					break;
				case 3:
					ry = 1;
					flipped = 1;
					single = 0;
					break;
				case 4:
					ry = 0;
					flipped = 1;
					single = 0;
					break;
				case 5:
					ry = 2;
					flipped = 0;
					single = 0;
					break;
				case 6:
					ry = 2;
					flipped = 1;
					single = 0;
					break;
			}
		}
		else
		{
			ry = 0;
			flipped = 0;
			single = 1;
		}

		x = 0;
		y++;

		clut_x = 320;
		tp_x = 320;

		v = ry * 84;

		do {
			u = x * 128;
		
			if (single)
			{
				skytexuv[i].u0 = u;
				skytexuv[i].v0 = v;
				skytexuv[i].u1 = u;
				skytexuv[i].v1 = v;
				skytexuv[i].u2 = u;
				skytexuv[i].v2 = v;
				skytexuv[i].u3 = u;
				skytexuv[i].v3 = v;
			}
			else if (flipped) 
			{
				skytexuv[i].u0 = u;
				skytexuv[i].v0 = v + 83;
				skytexuv[i].u1 = u + 127;
				skytexuv[i].v1 = v + 83;
				skytexuv[i].u2 = u;
				skytexuv[i].v2 = v;
				skytexuv[i].u3 = u + 127;
				skytexuv[i].v3 = v;
			}
			else
			{
				skytexuv[i].u0 = u;
				skytexuv[i].v0 = v;
				skytexuv[i].u1 = u + 127;
				skytexuv[i].v1 = v;
				skytexuv[i].u2 = u;
				skytexuv[i].v2 = v + 83;
				skytexuv[i].u3 = u + 127;
				skytexuv[i].v3 = v + 83;
			}
	
			skytpage[i] = GetTPage(0,0,tp_x & ~0x3f, ry * 84 & 768);
			skyclut[i] = GetClut(clut_x,ry + 252);

			tp_x += 32;
			clut_x += 16;

			x++;
			i++;
		} while (x < 4);
	
		flipped = (y < 7);
		
	} while (y < 7);

	skyNum = SkyNum();
	offset = SkySegment(gTimeOfDay) << 16;

	sprintf(name, "DATA\\SKY%d.RAW", skyNum);
	LoadfileSeg(name, (char*)_primTab1, offset, 0x10000);

	setRECT(&rect, 320, 0, 128, 256);

	LoadImage(&rect, (u_long*)_primTab1);

#ifndef PSX
	SkyHdBuild((const unsigned short*)_primTab1, skyHdTexture);
	SkyCycleRelease();
#endif

	DrawSync(0);
}

// [D] [T]
#if USE_PGXP
void DisplaySun(DVECTORF* pos, CVECTOR* col, int flare_col)
#else
void DisplaySun(DVECTOR* pos, CVECTOR* col, int flare_col)
#endif
{
	short sn, cs;
	int sz;

	POLY_FT4* polys;
	POLY_FT3* null;

	int u0, u1, v0, v2;
	short width, height;

	u0 = sun_texture.coords.u0;
	u1 = sun_texture.coords.u1;
	v0 = sun_texture.coords.v0;
	v2 = sun_texture.coords.v2;

	null = (POLY_FT3*)current->primptr;
	setPolyFT3(null);

	null->x0 = -1;
	null->y0 = -1;
	null->x1 = -1;
	null->y1 = -1;
	null->x2 = -1;
	null->y2 = -1;
	null->tpage = 0;

	addPrim(current->ot + 0x1079, null);
	current->primptr += sizeof(POLY_FT3);

	polys = (POLY_FT4*)current->primptr;
	setPolyFT4(polys);
	setSemiTrans(polys, 1);

	polys->r0 = col->r;
	polys->g0 = col->g;
	polys->b0 = col->b;

	width = (((u1 - (u0 - 1)) / 2) * 3) / 4;
	height = (((v2 - (v0 - 1)) / 2) * 3) / 4;

	polys->x0 = pos->vx - width;
	polys->y0 = pos->vy - height;
	polys->x1 = pos->vx + width;
	polys->y1 = pos->vy - height;
	polys->x2 = pos->vx - width;
	polys->y2 = pos->vy + height;
	polys->x3 = pos->vx + width;
	polys->y3 = pos->vy + height;

	polys->u0 = sun_texture.coords.u0;
	polys->v0 = sun_texture.coords.v0;
	polys->u1 = sun_texture.coords.u1;
	polys->v1 = sun_texture.coords.v1;
	polys->u2 = sun_texture.coords.u2;
	polys->v2 = sun_texture.coords.v2;
	polys->u3 = sun_texture.coords.u3;
	polys->v3 = sun_texture.coords.v3;
	polys->clut = sun_texture.clutid;
	polys->tpage = sun_texture.tpageid | 0x20;

	addPrim(current->ot + 0x1079, polys);
	current->primptr += sizeof(POLY_FT4);

	cs = RCOS(camera_angle.vy);  //rcossin_tbl[(camera_angle.vy & 0x1ffe) + 1];
	sn = RSIN(camera_angle.vy);  //rcossin_tbl[camera_angle.vy & 0x1ffe];

	sz = -(((flare_texture.coords.u1 - (flare_texture.coords.u0 - 1)) / 2) * 3) / 4;

	polys = (POLY_FT4*)current->primptr;
	setPolyFT4(polys);
	setSemiTrans(polys, 1);

	polys->r0 = (flare_col / 2);
	polys->g0 = (flare_col / 2);
	polys->b0 = (flare_col / 2);

	width = (cs - sn) * sz >> 0xc;
	height = (sn + cs) * sz >> 0xc;

	polys->x0 = pos->vx + width;
	polys->y0 = pos->vy + height;
	polys->x1 = pos->vx - height;
	polys->y1 = pos->vy + width;
	polys->x2 = pos->vx + height;
	polys->y2 = pos->vy - width;
	polys->x3 = pos->vx - width;
	polys->y3 = pos->vy - height;
	polys->u0 = flare_texture.coords.u0;
	polys->v0 = flare_texture.coords.v0;
	polys->u1 = flare_texture.coords.u1;
	polys->v1 = flare_texture.coords.v1;
	polys->u2 = flare_texture.coords.u2;
	polys->v2 = flare_texture.coords.v2;
	polys->u3 = flare_texture.coords.u3;
	polys->v3 = flare_texture.coords.v3;
	polys->clut = flare_texture.clutid;
	polys->tpage = flare_texture.tpageid | 0x20;

	addPrim(current->ot + 0x1078, polys);
	current->primptr += sizeof(POLY_FT4);
}

// [D] [T]
#if USE_PGXP
void DisplayMoon(DVECTORF* pos, CVECTOR* col, int flip)
#else
void DisplayMoon(DVECTOR* pos, CVECTOR* col, int flip)
#endif
{
	short width;
	short height;
	
	POLY_FT3* null;
	POLY_FT4* polys;
	
	null = (POLY_FT3*)current->primptr;
	setPolyFT3(null);

	null->x0 = -1;
	null->y0 = -1;
	null->x1 = -1;
	null->y1 = -1;
	null->x2 = -1;
	null->y2 = -1;
	null->tpage = 0;

	addPrim(current->ot + 0x1079, null);

	current->primptr += sizeof(POLY_FT3);

	polys = (POLY_FT4*)current->primptr;

	setPolyFT4(polys);
	setSemiTrans(polys, 1);

	polys->r0 = col->r;
	polys->g0 = col->g;
	polys->b0 = col->b;

	width = (moon_texture.coords.u1 - (moon_texture.coords.u0 - 1)) / 2;
	height = (moon_texture.coords.v2 - (moon_texture.coords.v0 - 1)) / 2;

	if (flip != 0)
		height = -height;

	polys->x0 = pos->vx - width;
	polys->y0 = pos->vy - height;
	polys->x1 = pos->vx + width;
	polys->y1 = pos->vy - height;
	polys->x2 = pos->vx - width;
	polys->y2 = pos->vy + height;
	polys->x3 = pos->vx + width;
	polys->y3 = pos->vy + height;

	polys->u0 = moon_texture.coords.u0;
	polys->v0 = moon_texture.coords.v0;
	polys->u1 = moon_texture.coords.u1;
	polys->v1 = moon_texture.coords.v1;
	polys->u2 = moon_texture.coords.u2;
	polys->v2 = moon_texture.coords.v2;
	polys->u3 = moon_texture.coords.u3;
	polys->v3 = moon_texture.coords.v3;

	polys->clut = moon_texture.clutid;
	polys->tpage = moon_texture.tpageid | 0x20;

	addPrim(current->ot + 0x1079, polys);
	current->primptr += sizeof(POLY_FT4);
}

extern VECTOR dummy;
RECT16 sun_source = {
		1008,
		456,
		16,
		10
};

int gSunShafts = 1;		// config.ini [render] sunShafts: god rays from the sun (PsyCross post pass), 0 = original picture

#ifndef PSX
// Queues the sun shafts pass behind the 3D scene (before the sun sample, lens flare and HUD).
// visibility 0..255: how much of the sun is seen; the sky mask of the pass hides the rest.
static void AddSunShafts(SVECTOR* sunDir, int visibility)
{
	static int smoothVisibility = 0;	// x16, eases the jumps of the flare sample
	extern int gShotFrame;

	int sunZ, fade, amount;
	SVECTOR horizonDir;
	DR_PSYX_SUNSHAFTS* shafts;
#if USE_PGXP
	DVECTORF sunXY, horizonXY;
#else
	DVECTOR sunXY, horizonXY;
#endif

	smoothVisibility += (visibility * 16 - smoothVisibility) / 8;

	gte_SetRotMatrix(&inv_camera_matrix);
	gte_SetTransVector(&dummy);

	gte_ldv0(sunDir);
	gte_rtps();
	gte_stsxy(&sunXY);
	gte_stszotz(&sunZ);

	// a far point at eye level in the direction of the sun lies on the horizon line
	horizonDir.vx = sunDir->vx;
	horizonDir.vy = 0;
	horizonDir.vz = sunDir->vz;
	gte_ldv0(&horizonDir);
	gte_rtps();
	gte_stsxy(&horizonXY);

	// fade out until the sun is 1.5 screen widths away from the centre
	fade = 0;
	if (sunZ > 0)
	{
		int dx = (int)sunXY.vx - 160;
		int dy = (int)sunXY.vy - 128;
		fade = 480 - SquareRoot0(dx * dx + dy * dy);
	}

	amount = fade > 0 ? (smoothVisibility / 16) * fade / 480 : 0;

	if (gShotFrame && FrameCnt == gShotFrame)
		printInfo("shot: sun shafts amount %d at %d %d, horizon %d\n", amount, (int)sunXY.vx, (int)sunXY.vy, (int)horizonXY.vy);

	if (amount <= 0)
		return;

	shafts = (DR_PSYX_SUNSHAFTS*)current->primptr;
	SetPsyXSunShafts(shafts, amount, (int)sunXY.vx, (int)sunXY.vy, (int)horizonXY.vy, 255, 240, 210);	// warm white
	addPrim(current->ot + 0x21, shafts);
	current->primptr += sizeof(DR_PSYX_SUNSHAFTS);
}
#endif

// [D] [T]
#ifndef PSX
int gFlareSmooth = 1;		// config.ini [render] lensFlareSmooth: the flare fades in and out as the sun hides behind things, instead of stepping with the sampled pixels
static int sFlareLevel;		// smoothed flare_col, in 1/16 steps
#endif

void DrawLensFlare(void)
{
	static char last_attempt_failed; // offset 0x0
	static ushort buffer[16*10];

	RECT16 viewp = { -1,-1,321,257 };

	int flarez;
	int distance_to_sun;
	int bufferY;
	int bufferX;
	POLY_FT4 *poly;
	FLAREREC *pFlareInfo;
	int shade;
	ushort *pwBuffer;
	DR_MOVE* sample_sun;
	int ygap;
	int xgap;
	int flare_col;
	int flaresize;
	
	int haze_col;

#if USE_PGXP
	DVECTORF sun_pers_conv_position;
#else
	DVECTOR sun_pers_conv_position;
#endif
	
	RECT16 source;
	CVECTOR col;

	source = sun_source;

	if (gWeather - 1U <= 1 || SkyOvercast())
	{
#ifndef PSX
		sFlareLevel = 0;
#endif
		return;
	}

	// the day night cycle moves the sun through dawn and dusk; the fixed sun is not shown in these skies
	if (skyCycleFrom >= 0 ? (gTimeOfDay != TIME_NIGHT && sun_position[BASE_CITY].vy > -300) : (M_BIT(gTimeOfDay) & (M_BIT(TIME_DAWN) | M_BIT(TIME_DUSK))) != 0)
		return;
	
	if (gTimeOfDay == TIME_NIGHT)
		col.r = 128;
	else
		col.r = 254;

	flare_col = 0;

	col.g = col.r;
	col.b = col.r;

	// a low sun is orange, white from about 18 degrees up
	if (skyCycleFrom >= 0 && gTimeOfDay != TIME_NIGHT)
	{
		int low = 255 - MIN(255, -sun_position[BASE_CITY].vy / 40);

		col.g = col.r - low * 90 / 255;
		col.b = col.r - low * 150 / 255;
	}

	// get the sun brightness from framebuffer copy
	if (gTimeOfDay != TIME_NIGHT && last_attempt_failed == 0)
	{
		pwBuffer = buffer;
		StoreImage(&source, (u_long*)buffer);

		bufferY = 0;
		do
		{
			bufferY++;
			bufferX = 0;

			do
			{
				if (*pwBuffer == 0xFFFF || *pwBuffer == 0x7fff)
					flare_col++;

				bufferX++;
				pwBuffer++;
			}
			while (bufferX < 12);

			pwBuffer += 4;
		}
		while (bufferY < 10);
	}

#ifndef PSX
	// the sample counts a handful of pixels: rise quickly, fall slowly
	if (gFlareSmooth)
	{
		int target = flare_col * 16;

		sFlareLevel += (target - sFlareLevel) / (target > sFlareLevel ? 3 : 8);

		if (target > sFlareLevel && target - sFlareLevel < 8) sFlareLevel = target;
		if (target < sFlareLevel && sFlareLevel - target < 16) sFlareLevel = target;

		flare_col = sFlareLevel / 16;
	}
#endif

	gte_SetRotMatrix(&inv_camera_matrix);
	gte_SetTransVector(&dummy);

	if (gTimeOfDay == TIME_NIGHT)
	{
		gte_ldv0(&moon_position[BASE_CITY]);
	}
	else
	{
		gte_ldv0(&sun_position[BASE_CITY]);
	}

	gte_rtps();

	gte_stsxy(&sun_pers_conv_position);

	gte_stszotz(&flarez);
	
	xgap = sun_pers_conv_position.vx - 160;
	ygap = sun_pers_conv_position.vy - 128;

	//sun_pers_conv_position.vy = (short)((u_int)sun_pers_conv_position >> 0x10);
	
	distance_to_sun = SquareRoot0(xgap * xgap + ygap * ygap);

	if (gTimeOfDay == TIME_NIGHT)
	{
		if (distance_to_sun < 500)
		{
			DisplayMoon(&sun_pers_conv_position, &col, 0);
			col.r = 64;
			col.b = 64;
			col.g = 64;
			
			gte_ldv0(&moon_shadow_position[BASE_CITY]);
			gte_rtps();
			gte_stsxy(&sun_pers_conv_position);

			DisplayMoon(&sun_pers_conv_position, &col, 1);
		}
	}
	else
	{
		if (flarez > 0 && distance_to_sun < 500)
		{
			DisplaySun(&sun_pers_conv_position, &col, flare_col);
			
			haze_col = (60 - (distance_to_sun / 2)) * flare_col;
			
			add_haze((haze_col / 6 + (haze_col >> 0x1f) >> 3) - (haze_col >> 0x1f), haze_col >> 6, 7);
			shade = flare_col / 3 - (distance_to_sun * 80) / 500;
	
			if (shade > -1 && flare_col != 0)
			{
				pFlareInfo = flare_info;
				
				do
				{
					int xpos, ypos, gapmod;
					
					gapmod = (128 - pFlareInfo->gapmod) * 2;
					flaresize = (pFlareInfo->size / 2);
					
					xpos = (xgap * gapmod) / 256;
					ypos = (ygap * gapmod) / 256;

					poly = (POLY_FT4*)current->primptr;
					setPolyFT4(poly);
					setSemiTrans(poly, 1);

					poly->x0 = poly->x2 = (xpos + 160) - flaresize;
					poly->y0 = poly->y1 = (ypos + 128) - flaresize;

					poly->x1 = poly->x3 = poly->x0 + pFlareInfo->size;
					poly->y2 = poly->y3 = poly->y0 + pFlareInfo->size;

					poly->r0 = (pFlareInfo->transparency.r * shade >> 6);
					poly->g0 = (pFlareInfo->transparency.g * shade >> 6);
					poly->b0 = (pFlareInfo->transparency.b * shade >> 6);
					
					poly->u0 = lensflare_texture.coords.u0;
					poly->v0 = lensflare_texture.coords.v0;
					poly->u1 = lensflare_texture.coords.u1;
					poly->v1 = lensflare_texture.coords.v1;
					poly->u2 = lensflare_texture.coords.u2;
					poly->v2 = lensflare_texture.coords.v2 - 4;
					poly->u3 = lensflare_texture.coords.u3;
					poly->v3 = lensflare_texture.coords.v3 - 4;
					
					poly->clut = lensflare_texture.clutid;
					poly->tpage = lensflare_texture.tpageid | 0x20;

					addPrim(current->ot + 10, poly);
					current->primptr += sizeof(POLY_FT4);

					pFlareInfo++;
				}
				while (pFlareInfo < flare_info + 8);
			}
		}

#ifndef PSX
		// a hidden or off screen sun still lights the sky around it (rays along building edges): the flare sample
		// only adds to a base visibility, the sky mask of the pass does the hiding
		if (gSunShafts)
			AddSunShafts(&sun_position[BASE_CITY], last_attempt_failed ? 128 : 128 + MIN(120, flare_col) * 127 / 120);
#endif

#if USE_PGXP
		// remap
		PsyX_GetPSXWidescreenMappedViewport(&viewp);
		sun_pers_conv_position.vx = RemapVal(sun_pers_conv_position.vx, float(viewp.x), float(viewp.w), 0.0f, 320.0f);
#endif
		
		sun_pers_conv_position.vx = sun_pers_conv_position.vx - 8;
		sun_pers_conv_position.vy = sun_pers_conv_position.vy - 4;
		
		// store framebuffer image of sun separately in VRAM
		if (sun_pers_conv_position.vx > -1 &&
			sun_pers_conv_position.vy > -1 &&
			sun_pers_conv_position.vx + 16 < 321 &&
			sun_pers_conv_position.vy + 10 < 257)
		{
			last_attempt_failed = 0;

			source.x = sun_pers_conv_position.vx;
			source.y = sun_pers_conv_position.vy + last->disp.disp.y;

#if 1//def PSX
			sample_sun = (DR_MOVE*)current->primptr;
			SetDrawMove(sample_sun, &source, 1008, 456);

			addPrim(current->ot + 0x20, sample_sun);
			current->primptr += sizeof(DR_MOVE);
#else
			// to avoid delays and uploads to GPU we're simply going to directly request from screen VRAM area
			sun_source = source;
#endif
		}
		else
		{
			last_attempt_failed = 1;
		}
	}
}

int gTunnelNum = -1;
int skyFade;

// [D] [T]
void TunnelSkyFade(void)
{
	int dX, dZ, diffZ, diffX;
	int len, l2;
	VECTOR* v2;
	VECTOR* v1;
	int tun;
	int px, pz;

	if (GameLevel != 3 && gTunnelNum < 3)
		tun = gTunnelNum;
	else
		tun = 2;

	v1 = NULL;
	v2 = NULL;

	// HMM?
	// DIFF_ANGLES(camera_angle.vy, tunnelDir[tun][0]) + 1247 < 2495;

	if (DIFF_ANGLES(camera_angle.vy, tunnelDir[tun][0]) + 1247 < 2495) // (((tunnelDir[tun][0] - camera_angle.vy) + 2048 & 4095) - 801 < 2495)
		v1 = &tunnelPos[tun][0];

	if (DIFF_ANGLES(camera_angle.vy, tunnelDir[tun][1]) + 1247 < 2495) // (((tunnelDir[tun][1] - camera_angle.vy) + 2048 & 4095) - 801 < 2495)
		v2 = &tunnelPos[tun][1];

	px = MainPlayer.pos[0];
	pz = MainPlayer.pos[2];

	if(v1 && v2)
	{
		dX = (v1->vx - px) >> 5;		// [A] smooth sky fade
		dZ = (v1->vz - pz) >> 5;

		diffX = (v2->vx - px) >> 5;
		diffZ = (v2->vz - pz) >> 5;

		len = (dX * dX + dZ * dZ);
		l2 = (diffX * diffX + diffZ * diffZ);

		if (l2 <= len)
			len = l2;
	}
	else if(v2)
	{
		diffX = (v2->vx - px) >> 5;
		diffZ = (v2->vz - pz) >> 5;

		len = (diffX * diffX + diffZ * diffZ);
	}
	else if(v1)
	{
		diffX = (v1->vx - px) >> 5;
		diffZ = (v1->vz - pz) >> 5;

		len = (diffX * diffX + diffZ * diffZ);
	}
	else
	{
		skyFade = 0;
		return;
	}

	// [A] smooth sky fade
	len >>= 12;

	if (len <= 128)
		skyFade = -len + 128;
	else
		skyFade = 0;
}

// [D] [T]
void calc_sky_brightness(RGB16* skycolor)
{
	int dawn;
	dawn = DawnCount >> 5;

	// the day night cycle sets the brightness of each sky it fades (DrawSkyDome)
	if((M_BIT(gTimeOfDay) & (M_BIT(TIME_DAWN) | M_BIT(TIME_DUSK))) && skyCycleFrom < 0)
	{
		if (gTimeOfDay == TIME_DAWN)
		{
			skycolor->r = dawn + 41;
			skycolor->b = dawn + 28;
		}
		else if (gTimeOfDay == TIME_DUSK)
		{
			skycolor->r = 143 - dawn;
			skycolor->b = 128 - dawn;
		}

		if (skycolor->r < 26)
			skycolor->r = 26;
		else if (skycolor->r > 128)
			skycolor->r = 128;

		if (skycolor->b < 26)
			skycolor->b = 26;
		else if (skycolor->b > 128)
			skycolor->b = 128;

		skycolor->g = skycolor->b;
	}
	else
	{
		skycolor->b = 128;
		skycolor->g = 128;
		skycolor->r = 128;
	}
	
	if (gTunnelNum == -1 || 
		GameLevel == 0 ||
		GameLevel == 1 && gTunnelNum == 2 ||
		GameLevel == 2 ||
		GameLevel == 3 && gTunnelNum != 1)
		return;

	TunnelSkyFade();

	if (skycolor->r > skyFade)
		skycolor->r = skyFade;

	if (skycolor->g > skyFade)
		skycolor->g = skyFade;

	if (skycolor->b > skyFade)
		skycolor->b = skyFade;
}

#if USE_PGXP
DVECTORF scratchPad_skyVertices[35];	// 1f800044
#else
#define scratchPad_skyVertices ((DVECTOR*)getScratchAddr(0x11))	// 1f800044
static_assert(0x11 + sizeof(DVECTOR) * 35 < 1024 - sizeof(_pct), "scratchpad overflow");
#endif

// [D] [T]
void PlotSkyPoly(POLYFT4* polys, int skytexnum, unsigned char r, unsigned char g, unsigned char b)
{
	POLYFT4* src;
	POLY_FT4* poly;

	src = polys;
	poly = (POLY_FT4*)current->primptr;

#if USE_PGXP
	DVECTORF* outpoints = scratchPad_skyVertices;
#else
	DVECTOR* outpoints = scratchPad_skyVertices;
#endif

#ifdef PSX
	if ((outpoints[src->v0].vy > -1 || outpoints[src->v1].vy > -1 || outpoints[src->v2].vy > -1 || outpoints[src->v3].vy > -1) &&
		(outpoints[src->v0].vx > -1 || outpoints[src->v1].vx > -1 || outpoints[src->v2].vx > -1 || outpoints[src->v3].vx > -1) &&
		(outpoints[src->v0].vx < 321 || outpoints[src->v1].vx < 321 || outpoints[src->v2].vx < 321 || outpoints[src->v3].vx < 321))
#endif
	{
		setPolyFT4(poly);

		poly->r0 = r;
		poly->g0 = g;
		poly->b0 = b;

		*(u_int*)&poly->x0 = *(u_int*)&outpoints[src->v0].vx;
		*(u_int*)&poly->x1 = *(u_int*)&outpoints[src->v1].vx;
		*(u_int*)&poly->x2 = *(u_int*)&outpoints[src->v3].vx;
		*(u_int*)&poly->x3 = *(u_int*)&outpoints[src->v2].vx;
	
		*(ushort*)&poly->u0 = *(ushort*)&skytexuv[skytexnum].u2;
		*(ushort*)&poly->u1 = *(ushort*)&skytexuv[skytexnum].u3;
		*(ushort*)&poly->u2 = *(ushort*)&skytexuv[skytexnum].u0;
		*(ushort*)&poly->u3 = *(ushort*)&skytexuv[skytexnum].u1;
		
		poly->clut = skyclut[skytexnum];
		poly->tpage = NO_SNOW_TPAGE(skytpage[skytexnum]);

#ifndef PSX
		if (skyDrawAdd)
		{
			setSemiTrans(poly, 1);
			poly->tpage |= 1 << 5;	// semi transparency mode 1: background + quad
		}
#endif

		addPrim(current->ot + OTSIZE - 1, poly);

#if USE_PGXP && USE_EXTENDED_PRIM_POINTERS
		poly->pgxp_index = outpoints[src->v0].pgxp_index;
#endif 

		current->primptr += sizeof(POLY_FT4);

#ifndef PSX
		// the packet goes in after the quad: the chain runs against the insertion order, so it is read before the quad
		if (skyDrawTexture[0])
		{
			DR_PSYX_TEX* tex = (DR_PSYX_TEX*)current->primptr;

			SetPsyXTexture(tex, skyDrawTexture[(skytexnum & 3) >> 1], 256, 256);
			addPrim(current->ot + OTSIZE - 1, tex);
			current->primptr += sizeof(DR_PSYX_TEX);
		}
#endif
	}
}

// [D] [T]
void PlotHorizonMDL(MODEL* model, int horizontaboffset, RGB16* skycolor)
{
	SVECTOR* verts;

#if USE_PGXP
	DVECTORF* dv;
#else
	DVECTOR* dv;
#endif

	int count;
	unsigned char* polys;
	int red, green, blue;
	int z;

	z = -1;
	verts = GET_MODEL_DATA(SVECTOR, model, vertices);
	dv = scratchPad_skyVertices;
	count = model->num_vertices;

#if USE_PGXP
	PGXP_SetZOffsetScale(0.0f, 256.0f);
#endif

	do
	{
		gte_ldv3(verts, verts+1, verts+2);
		gte_rtpt();
		gte_stsxy3(dv, dv+1, dv+2);

		if(count == 15)
			gte_stszotz(&z);

#if USE_PGXP
		// store PGXP index
		// HACK: -1 is needed here for some reason
		dv[0].pgxp_index = dv[1].pgxp_index = dv[2].pgxp_index = PGXP_GetIndex(0) - 1;
#endif
		dv += 3;
		verts += 3;
		count -= 3;
	} while (count);

#if USE_PGXP
	PGXP_SetZOffsetScale(0.0f, 1.0f);
#endif

	if (z > 0)
	{
		int polySize;
		u_char* horizonTex = &HorizonTextures[horizontaboffset];
		polys = GET_MODEL_DATA(u_char, model, poly_block);
		polySize = PolySizes[*polys];

		red = skycolor->r;
		green = skycolor->g;
		blue = skycolor->b;
		
		// draw sky
		count = model->num_polys;
		do
		{
			PlotSkyPoly((POLYFT4*)polys, *horizonTex++, red, green, blue);
			polys += polySize;
		} while (--count > 8);

		red /= 2;
		green /= 2;
		blue /= 2;

		// draw it's reflection
		do
		{
			PlotSkyPoly((POLYFT4*)polys, *horizonTex++, red, green, blue);
			polys += polySize;
		} while (--count);
	}
}

static void PlotSkyHorizons(RGB16* skycolor);
#ifndef PSX
static void PlotSkyPass(TextureID* texture, RGB16* skycolor, float light, int add);
#endif

// [D] [T]
void DrawSkyDome(void)
{
	RGB16 skycolor = { 128,128,128 };
	VECTOR skyOfs = dummy;

	calc_sky_brightness(&skycolor);

#ifndef PSX
	if (skyCycleFrom >= 0 && !skyCycleBuilt)
		SkyCycleBuild();

	// inserted first, so it is read after every sky quad and ends the HD texture
	if (skyHdTexture[0] || SkyCycleReady())
	{
		DR_PSYX_TEX* tex = (DR_PSYX_TEX*)current->primptr;

		SetPsyXTexture(tex, 0, 0, 0);
		addPrim(current->ot + OTSIZE - 1, tex);
		current->primptr += sizeof(DR_PSYX_TEX);
	}
#endif

	skyOfs.vy = sky_y_offset[BASE_CITY];
	gte_SetRotMatrix(&inv_camera_matrix);
	gte_SetTransVector(&skyOfs);

#ifndef PSX
	// day night cycle: the 'from' sky dimmed by 1 - t, the 'to' sky added on top dimmed by t. The quads inserted
	// last are read first, so the added pass goes in before the base pass.
	if (skyCycleFrom >= 0 && SkyCycleReady())
	{
		if (skyCycleT > 0.0f)
			PlotSkyPass(skyCycleTexture[SkySegment(skyCycleTo)], &skycolor, skyCycleLight[1] * skyCycleT, skyCycleT < 1.0f);

		if (skyCycleT < 1.0f)
			PlotSkyPass(skyCycleTexture[SkySegment(skyCycleFrom)], &skycolor, skyCycleLight[0] * (1.0f - skyCycleT), 0);

		return;
	}
#endif

	PlotSkyHorizons(&skycolor);
}

#ifndef PSX
static void PlotSkyPass(TextureID* texture, RGB16* skycolor, float light, int add)
{
	RGB16 color;

	color.r = (short)(skycolor->r * light / 128.0f);
	color.g = (short)(skycolor->g * light / 128.0f);
	color.b = (short)(skycolor->b * light / 128.0f);

	skyDrawTexture = texture;
	skyDrawAdd = add;

	PlotSkyHorizons(&color);

	skyDrawTexture = skyHdTexture;
	skyDrawAdd = 0;
}
#endif

static void PlotSkyHorizons(RGB16* skycolor)
{
#ifdef PSX
	// FIXME: use frustrum angle instead?
	if (((camera_angle.vy - 1450U) & 4095) > 2250)
		PlotHorizonMDL(modelpointers[0], HorizonLookup[BASE_CITY][0], skycolor);

	if (((camera_angle.vy - 651U) & 4095) < 1799)
		PlotHorizonMDL(modelpointers[2], HorizonLookup[BASE_CITY][1], skycolor);

	if (((camera_angle.vy - 1701U) & 4095) < 1749)
		PlotHorizonMDL(modelpointers[3], HorizonLookup[BASE_CITY][2], skycolor);

	if (((camera_angle.vy - 400U) & 4095) > 2300)
		PlotHorizonMDL(modelpointers[1], HorizonLookup[BASE_CITY][3], skycolor);
#else
	// draw full sky - no need in frustrum culling
	PlotHorizonMDL(modelpointers[0], HorizonLookup[BASE_CITY][0], skycolor);
	PlotHorizonMDL(modelpointers[2], HorizonLookup[BASE_CITY][1], skycolor);
	PlotHorizonMDL(modelpointers[3], HorizonLookup[BASE_CITY][2], skycolor);
	PlotHorizonMDL(modelpointers[1], HorizonLookup[BASE_CITY][3], skycolor);
#endif
}

