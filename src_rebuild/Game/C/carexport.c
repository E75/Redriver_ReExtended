#include "driver2.h"

#include "carexport.h"
#include "cars.h"
#include "cosmetic.h"
#include "texture.h"
#include "models.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#define _mkdir(p) mkdir(p, 0777)
#endif

#ifndef PSX

// Car export, see carexport.h. Writes <dir>/<id>/car.json and car.glb of the car in a resident slot.
// The glb holds the clean body with its texture page (every page and palette the polygons use, cut to the used rectangle and
// packed in one atlas). The body is in the car frame (vertex + cog) and rotated to the glTF frame: (x, -y, -z).

extern char* LevelNames[];

#define ATLAS_W		256
#define MAX_TILES	24
#define MAX_TRIS	4096
#define WHITE_H		4
#define WHEEL_HUB_X	17		// DrawCarWheels: the wheel model sits 17 units to the side of the wheel position

typedef struct
{
	int tpage, clut;
	int umin, vmin, umax, vmax;
	int yoff;
} ATLAS_TILE;

typedef struct
{
	short v[3];
	short tile;		// -1 = flat colour
	unsigned char u[3], vv[3];
	unsigned char col[4];
} TRI;

static ATLAS_TILE tiles[MAX_TILES];
static int numTiles;
static TRI tris[MAX_TRIS];
static int numTris;

// ---- PNG (stored deflate blocks, no compression) ----

static unsigned int crcTable[256];

static unsigned int Crc(unsigned int crc, const unsigned char* d, int n)
{
	int i;

	if (!crcTable[1])
	{
		unsigned int c, k;

		for (i = 0; i < 256; i++)
		{
			c = i;

			for (k = 0; k < 8; k++)
				c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;

			crcTable[i] = c;
		}
	}

	crc = ~crc;

	for (i = 0; i < n; i++)
		crc = crcTable[(crc ^ d[i]) & 0xff] ^ (crc >> 8);

	return ~crc;
}

static void Put32(unsigned char* p, unsigned int v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

static unsigned char* PngChunk(unsigned char* o, const char* type, const unsigned char* data, int n)
{
	Put32(o, n);
	memcpy(o + 4, type, 4);

	if (n)
		memcpy(o + 8, data, n);

	Put32(o + 8 + n, Crc(Crc(0, o + 4, 4), data, n));
	return o + 12 + n;
}

// returns a malloc'd PNG of an RGBA image
static unsigned char* MakePng(const unsigned char* rgba, int w, int h, int* outSize)
{
	int rowBytes = w * 4 + 1;
	int rawSize = rowBytes * h;
	int blocks = (rawSize + 65534) / 65535;
	int zSize = 2 + rawSize + blocks * 5 + 4;
	unsigned char* raw = (unsigned char*)malloc(rawSize);
	unsigned char* z = (unsigned char*)malloc(zSize);
	unsigned char* png = (unsigned char*)malloc(zSize + 100);
	unsigned char ihdr[13];
	unsigned char* o = png;
	unsigned char* zp = z;
	unsigned int s1 = 1, s2 = 0;
	int y, i, left = rawSize, pos = 0;

	for (y = 0; y < h; y++)
	{
		raw[y * rowBytes] = 0;
		memcpy(raw + y * rowBytes + 1, rgba + y * w * 4, w * 4);
	}

	for (i = 0; i < rawSize; i++)
	{
		s1 = (s1 + raw[i]) % 65521;
		s2 = (s2 + s1) % 65521;
	}

	*zp++ = 0x78;
	*zp++ = 0x01;

	while (left > 0)
	{
		int n = left > 65535 ? 65535 : left;

		*zp++ = left == n;
		*zp++ = n & 0xff;
		*zp++ = n >> 8;
		*zp++ = ~n & 0xff;
		*zp++ = (~n >> 8) & 0xff;
		memcpy(zp, raw + pos, n);
		zp += n;
		pos += n;
		left -= n;
	}

	Put32(zp, (s2 << 16) | s1);
	zp += 4;

	memcpy(o, "\x89PNG\r\n\x1a\n", 8);
	o += 8;
	Put32(ihdr, w);
	Put32(ihdr + 4, h);
	ihdr[8] = 8;
	ihdr[9] = 6;
	ihdr[10] = ihdr[11] = ihdr[12] = 0;
	o = PngChunk(o, "IHDR", ihdr, 13);
	o = PngChunk(o, "IDAT", z, (int)(zp - z));
	o = PngChunk(o, "IEND", NULL, 0);

	*outSize = (int)(o - png);
	free(raw);
	free(z);
	return png;
}

// ---- texture pages ----

// the used rectangle of one page + palette as RGBA; alpha 0 for the transparent colour 0
static void DecodeTile(const ATLAS_TILE* t, unsigned char* dst)
{
	int px = (t->tpage & 15) * 64, py = ((t->tpage >> 4) & 1) * 256, mode = (t->tpage >> 7) & 3;
	int ww = mode == 0 ? 64 : mode == 1 ? 128 : 256;
	unsigned short pal[256], *page = (unsigned short*)malloc(ww * 256 * 2);
	RECT16 r;
	int u, v;

	r.x = (t->clut & 63) * 16;
	r.y = t->clut >> 6;
	r.w = mode == 0 ? 16 : 256;
	r.h = 1;
	StoreImage(&r, (u_long*)pal);

	r.x = px;
	r.y = py;
	r.w = ww;
	r.h = 256;
	StoreImage(&r, (u_long*)page);

	for (v = t->vmin; v <= t->vmax; v++)
	{
		for (u = t->umin; u <= t->umax; u++)
		{
			unsigned short word = mode == 0 ? page[v * ww + u / 4] : mode == 1 ? page[v * ww + u / 2] : page[v * ww + u];
			unsigned short col = mode == 0 ? pal[(word >> ((u & 3) * 4)) & 15] : mode == 1 ? pal[(word >> ((u & 1) * 8)) & 255] : word;
			unsigned char* o = dst + ((v - t->vmin) * ATLAS_W + u) * 4;

			o[0] = (col & 31) << 3 | (col & 31) >> 2;
			o[1] = ((col >> 5) & 31) << 3 | ((col >> 5) & 31) >> 2;
			o[2] = ((col >> 10) & 31) << 3 | ((col >> 10) & 31) >> 2;
			o[3] = col == 0 ? 0 : 255;
		}
	}

	free(page);
}

static int FindTile(int tpage, int clut, const unsigned char* u, const unsigned char* v, int n)
{
	int i, k;

	for (i = 0; i < numTiles; i++)
	{
		if (tiles[i].tpage == tpage && tiles[i].clut == clut)
			break;
	}

	if (i == numTiles)
	{
		if (numTiles >= MAX_TILES)
			return -1;

		tiles[i].tpage = tpage;
		tiles[i].clut = clut;
		tiles[i].umin = tiles[i].vmin = 255;
		tiles[i].umax = tiles[i].vmax = 0;
		numTiles++;
	}

	for (k = 0; k < n; k++)
	{
		tiles[i].umin = MIN(tiles[i].umin, u[k]);
		tiles[i].umax = MAX(tiles[i].umax, u[k]);
		tiles[i].vmin = MIN(tiles[i].vmin, v[k]);
		tiles[i].vmax = MAX(tiles[i].vmax, v[k]);
	}

	return i;
}

static void AddTri(unsigned int indices, int tpage, int clut, const unsigned short* uv, const unsigned char* col)
{
	TRI* t;
	unsigned char u[3], v[3];
	int k;

	if (numTris >= MAX_TRIS)
		return;

	t = &tris[numTris];

	for (k = 0; k < 3; k++)
		t->v[k] = (indices >> (k * 8)) & 0xff;

	if (uv)
	{
		for (k = 0; k < 3; k++)
		{
			u[k] = uv[k] & 0xff;
			v[k] = uv[k] >> 8;
		}

		t->tile = FindTile(tpage, clut, u, v, 3);
		memcpy(t->u, u, 3);
		memcpy(t->vv, v, 3);
	}
	else
		t->tile = -1;

	memcpy(t->col, col, 4);
	numTris++;
}

// the polygons of the car as the game draws them (cars.c buildNewCarFromModel): textured triangles with their texture rectangle,
// the flat ones in dark grey
static void CollectTris(const CAR_MODEL* car)
{
	static const unsigned char white[4] = { 255, 255, 255, 255 };
	static const unsigned char dark[4] = { 24, 24, 24, 255 };
	int i, pass;

	numTiles = 0;
	numTris = 0;

	for (pass = 0; pass < 3; pass++)
	{
		const CAR_POLY* p = pass == 0 ? car->pGT3 : pass == 1 ? car->pFT3 : car->pB3;
		int n = pass == 0 ? car->numGT3 : pass == 1 ? car->numFT3 : car->numB3;

		for (i = 0; i < n; i++, p++)
		{
			unsigned short uv[3];
			int clut;

			if (pass == 2)
			{
				AddTri(p->vindices, 0, 0, NULL, dark);
				continue;
			}

			uv[0] = p->clut_uv0 & 0xffff;
			uv[1] = p->tpage_uv1 & 0xffff;
			uv[2] = p->uv3_uv2 & 0xffff;

			// gouraud polygons index the palette table of the civilian cars, flat ones carry the palette
			clut = pass == 0 ? ((unsigned short*)civ_clut)[sizeof(civ_clut[0]) / 2 + (p->clut_uv0 >> 16)] : p->clut_uv0 >> 16;
			AddTri(p->vindices, (p->tpage_uv1 >> 16) & 0x3fff, clut, uv, white);
		}
	}
}

// ---- glb ----

static void Pad4(FILE* f, int n, int fill)
{
	while (n & 3)
	{
		fputc(fill, f);
		n++;
	}
}

static int WriteGlb(const char* path, const char* name, MODEL* model, const CAR_COSMETICS* cc)
{
	const SVECTOR* verts = GET_MODEL_DATA(SVECTOR, model, vertices);
	int n = numTris * 3, atlasH = WHITE_H, i, k, pngSize, jsonLen, binLen;
	unsigned char* atlas;
	unsigned char* png;
	float* pos = (float*)malloc(n * 12);
	float* uvs = (float*)malloc(n * 8);
	unsigned char* cols = (unsigned char*)malloc(n * 4);
	float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
	char* json = (char*)malloc(4096);
	unsigned int hdr[3], chunk[2];
	FILE* f;

	for (i = 0; i < numTiles; i++)
	{
		tiles[i].umax = MIN(255, tiles[i].umax + 1);
		tiles[i].vmax = MIN(255, tiles[i].vmax + 1);
		tiles[i].yoff = atlasH;
		atlasH += tiles[i].vmax - tiles[i].vmin + 1;
	}

	atlas = (unsigned char*)calloc(ATLAS_W * atlasH, 4);
	memset(atlas, 255, ATLAS_W * WHITE_H * 4);

	for (i = 0; i < numTiles; i++)
	{
		DecodeTile(&tiles[i], atlas + tiles[i].yoff * ATLAS_W * 4);
	}

	png = MakePng(atlas, ATLAS_W, atlasH, &pngSize);

	for (i = 0; i < numTris; i++)
	{
		for (k = 0; k < 3; k++)
		{
			// the engine winding is the reverse of glTF: corners 1 and 2 swap
			int src = k == 0 ? 0 : 3 - k;
			const SVECTOR* s = &verts[tris[i].v[src]];
			float* p = &pos[(i * 3 + k) * 3];
			float* t = &uvs[(i * 3 + k) * 2];
			int c;

			p[0] = (float)(s->vx + cc->cog.vx);
			p[1] = -(float)(s->vy + cc->cog.vy);
			p[2] = -(float)(s->vz + cc->cog.vz);

			for (c = 0; c < 3; c++)
			{
				mn[c] = MIN(mn[c], p[c]);
				mx[c] = MAX(mx[c], p[c]);
			}

			if (tris[i].tile >= 0)
			{
				const ATLAS_TILE* tl = &tiles[tris[i].tile];

				t[0] = (tris[i].u[src] + 0.5f) / ATLAS_W;
				t[1] = (tl->yoff + tris[i].vv[src] - tl->vmin + 0.5f) / atlasH;
			}
			else
			{
				t[0] = 2.0f / ATLAS_W;
				t[1] = 2.0f / atlasH;
			}

			memcpy(&cols[(i * 3 + k) * 4], tris[i].col, 4);
		}
	}

	binLen = n * 12 + n * 8 + n * 4 + pngSize;

	jsonLen = sprintf(json,
		"{\"asset\":{\"version\":\"2.0\",\"generator\":\"redriver2 car export\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
		"\"nodes\":[{\"mesh\":0,\"name\":\"%s\"}],"
		"\"meshes\":[{\"name\":\"%s\",\"primitives\":[{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1,\"COLOR_0\":2},\"material\":0}]}],"
		"\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},\"metallicFactor\":0,\"roughnessFactor\":1},\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5}],"
		"\"textures\":[{\"source\":0,\"sampler\":0}],\"samplers\":[{\"magFilter\":9728,\"minFilter\":9728}],"
		"\"images\":[{\"bufferView\":3,\"mimeType\":\"image/png\"}],"
		"\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\",\"min\":[%g,%g,%g],\"max\":[%g,%g,%g]},"
		"{\"bufferView\":1,\"componentType\":5126,\"count\":%d,\"type\":\"VEC2\"},"
		"{\"bufferView\":2,\"componentType\":5121,\"normalized\":true,\"count\":%d,\"type\":\"VEC4\"}],"
		"\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%d},{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":%d},"
		"{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":%d},{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":%d}],"
		"\"buffers\":[{\"byteLength\":%d}]}",
		name, name, n, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2], n, n,
		n * 12, n * 12, n * 8, n * 20, n * 4, n * 24, pngSize, binLen);

	f = fopen(path, "wb");

	if (f)
	{
		int jsonPad = (jsonLen + 3) & ~3, binPad = (binLen + 3) & ~3;

		hdr[0] = 0x46546C67;
		hdr[1] = 2;
		hdr[2] = 12 + 8 + jsonPad + 8 + binPad;
		fwrite(hdr, 4, 3, f);

		chunk[0] = jsonPad;
		chunk[1] = 0x4E4F534A;
		fwrite(chunk, 4, 2, f);
		fwrite(json, 1, jsonLen, f);
		Pad4(f, jsonLen, ' ');

		chunk[0] = binPad;
		chunk[1] = 0x004E4942;
		fwrite(chunk, 4, 2, f);
		fwrite(pos, 12, n, f);
		fwrite(uvs, 8, n, f);
		fwrite(cols, 4, n, f);
		fwrite(png, 1, pngSize, f);
		Pad4(f, binLen, 0);
		fclose(f);
	}

	free(pos);
	free(uvs);
	free(cols);
	free(json);
	free(atlas);
	free(png);
	return f != NULL;
}

// ---- car.json ----

// car.json stays in engine coordinates
static void Vec(FILE* f, int x, int y, int z)
{
	fprintf(f, "[%d, %d, %d]", x, y, z);
}

// lamp pair: the cosmetic value is relative to the cog, the other lamp is mirrored over x = cog.x (cosmetic.c)
static void Pair(FILE* f, const char* key, const SVECTOR* l, const SVECTOR* cog, int last)
{
	int xa = l->vx + cog->vx, xb = cog->vx - l->vx;
	int y = l->vy + cog->vy, z = l->vz + cog->vz;

	fprintf(f, "    \"%s\": { \"L\": ", key);
	Vec(f, MAX(xa, xb), y, z);
	fprintf(f, ", \"R\": ");
	Vec(f, MIN(xa, xb), y, z);
	fprintf(f, " }%s\n", last ? "" : ",");
}

static void Raw(FILE* f, const char* key, const SVECTOR* v, int last)
{
	fprintf(f, "    \"%s\": ", key);
	Vec(f, v->vx, v->vy, v->vz);
	fprintf(f, "%s\n", last ? "" : ",");
}

static int WriteJson(const char* path, const char* id, const char* name, int city, int model, const CAR_COSMETICS* cc, const int* comp)
{
	FILE* f = fopen(path, "wb");
	int w;

	if (!f)
		return 0;

	fprintf(f, "{\n  \"schema\": \"redriver-car/1\",\n  \"id\": \"%s\",\n  \"name\": \"%s\",\n", id, name);
	fprintf(f, "  \"source\": { \"city\": \"%s\", \"model\": %d },\n", LevelNames[city], model);
	fprintf(f, "  \"units\": \"engine\",\n");
	fprintf(f, "  \"frame\": \"engine car frame: x sideways, y down (up is negative), z along the car, front is -z; origin = car position (body vertex + cog)\",\n");
	fprintf(f, "  \"mesh\": { \"file\": \"car.glb\", \"transform\": \"glb = (x, -y, -z) of the engine frame, triangle corners 1 and 2 swapped\" },\n");
	fprintf(f, "  \"cog\": ");
	Vec(f, cc->cog.vx, cc->cog.vy, cc->cog.vz);
	fprintf(f, ",\n  \"wheelRadius\": %d,\n", cc->wheelSize);

	// wheels: DrawCarWheels: y = -radius - disp.y - compression + 14. 0 and 1 are the +17 side, even = front axle
	fprintf(f, "  \"wheels\": [\n");

	for (w = 0; w < 4; w++)
	{
		int x = ((w & 2) ? -WHEEL_HUB_X : WHEEL_HUB_X) - cc->wheelDisp[w].vx;
		int y = -cc->wheelSize - cc->wheelDisp[w].vy - comp[w] + 14;
		int z = -cc->wheelDisp[w].vz;

		fprintf(f, "    { \"axle\": \"%s\", \"hubX\": %d, \"compression\": %d, \"pos\": ", (w & 1) ? "rear" : "front", (w & 2) ? -WHEEL_HUB_X : WHEEL_HUB_X, comp[w]);
		Vec(f, x, y, z);
		fprintf(f, " }%s\n", w < 3 ? "," : "");
	}

	fprintf(f, "  ],\n  \"lamps\": {\n");
	Pair(f, "headlight", &cc->headLight, &cc->cog, 0);
	Pair(f, "brake", &cc->brakeLight, &cc->cog, 0);
	Pair(f, "indicatorFront", &cc->frontInd, &cc->cog, 0);
	Pair(f, "indicatorBack", &cc->backInd, &cc->cog, 0);
	Pair(f, "reverse", &cc->revLight, &cc->cog, 0);

	fprintf(f, "    \"exhaust\": ");
	Vec(f, cc->exhaust.vx + cc->cog.vx, cc->exhaust.vy + cc->cog.vy, cc->exhaust.vz + cc->cog.vz);
	fprintf(f, ",\n    \"police\": ");
	Vec(f, cc->cog.vx, cc->policeLight.vy + cc->cog.vy, cc->policeLight.vz + cc->cog.vz);
	fprintf(f, "\n  },\n");

	// the values as the game holds them (cosmetic.c CAR_COSMETICS), for the import
	fprintf(f, "  \"raw\": {\n");
	Raw(f, "headLight", &cc->headLight, 0);
	Raw(f, "frontInd", &cc->frontInd, 0);
	Raw(f, "backInd", &cc->backInd, 0);
	Raw(f, "brakeLight", &cc->brakeLight, 0);
	Raw(f, "revLight", &cc->revLight, 0);
	Raw(f, "policeLight", &cc->policeLight, 0);
	Raw(f, "exhaust", &cc->exhaust, 0);
	Raw(f, "smoke", &cc->smoke, 0);
	Raw(f, "fire", &cc->fire, 0);
	Raw(f, "cog", &cc->cog, 0);
	fprintf(f, "    \"wheelSize\": %d,\n    \"wheelDisp\": [", cc->wheelSize);

	for (w = 0; w < 4; w++)
	{
		Vec(f, cc->wheelDisp[w].vx, cc->wheelDisp[w].vy, cc->wheelDisp[w].vz);
		fprintf(f, "%s", w < 3 ? ", " : "");
	}

	fprintf(f, "]\n  }\n}\n");
	fclose(f);
	return 1;
}

int CarExportWrite(const char* dir, const char* id, const char* name, int city, int model, int slot, const int* comp)
{
	char path[512];
	MODEL* clean = gCarCleanModelPtr[slot];
	const CAR_COSMETICS* cc = &car_cosmetics[slot];

	if (!clean)
		return 0;

	sprintf(path, "%s/%s", dir, id);
	_mkdir(path);

	CollectTris(&NewCarModel[slot]);

	sprintf(path, "%s/%s/car.glb", dir, id);

	if (!WriteGlb(path, id, clean, cc))
		return 0;

	sprintf(path, "%s/%s/car.json", dir, id);
	return WriteJson(path, id, name, city, model, cc, comp);
}

#endif // !PSX
