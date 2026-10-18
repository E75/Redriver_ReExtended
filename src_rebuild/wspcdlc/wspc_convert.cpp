#include "wspc_convert.h"
#include "wspcdlc.h"
#include "../d1dlc/d1_util.h"

#include <math.h>
#include <algorithm>
#include <map>
#include <set>

// Port of tools/wspc/wspc2car.py (checked against it). Node positions are added to the vertices, source units are used 1:1.

namespace
{
const int MODEL_HEADER = 36;
const int POLY_FT3 = 20, POLY_FT4 = 21;		// Redriver polygon ids, 20 bytes each
const int MAX_VERTS = 256;					// polygon vertex numbers are one byte
const int MAX_PAGES = 2;					// a car owns two texture pages
const int MAX_PALETTES = 64;				// palette room of the special car slot
const int PAGE_BYTES = 0x8000;

// bounds checked little endian access to the file
struct Bytes
{
	const uint8_t* p;
	size_t size;
	mutable bool bad = false;

	template<typename T>
	T Get(size_t at) const
	{
		T v = T();
		if (at + sizeof(T) > size)
			bad = true;
		else
			memcpy(&v, p + at, sizeof(T));
		return v;
	}

	uint8_t U8(size_t at) const { return Get<uint8_t>(at); }
};

struct Node { int poolOffset; int pos[3]; int parent; };
struct Material { int page, clut; uint8_t uv[4][2]; };
struct Prim { std::vector<int> idx; int colour[4][3]; int material; };
struct Mesh { std::vector<std::vector<int>> verts; std::vector<Material> materials; std::vector<Prim> prims; };
struct Chunk { size_t off, size; uint32_t tag; };

void ParseChunks(const Bytes& d, size_t off, size_t end, std::vector<Chunk>& out)
{
	while (off + 8 <= end)
	{
		uint32_t size = d.Get<uint32_t>(off), tag = d.Get<uint32_t>(off + 4);
		if (size < 8 || off + size > end)
			break;

		out.push_back({ off, size, tag });
		if (tag == 1 || tag == 2 || tag == 4)
			ParseChunks(d, off + 8, off + size, out);

		off += size;
	}
}

bool ReadMesh(const Bytes& d, size_t pool, int offset, Mesh& mesh)
{
	size_t base = pool + offset;
	int vertexCount = (int)d.Get<uint32_t>(base + 4);
	int materialCount = d.Get<int16_t>(base + 8);
	size_t vertexAt = base + d.Get<uint32_t>(base + 0x18);
	size_t materialAt = base + d.Get<uint32_t>(base + 0x10);
	size_t subAt = base + d.Get<uint32_t>(base + 0xc);

	if (d.bad || vertexCount < 0 || vertexCount > 4096 || materialCount < 0 || materialCount > 4096)
		return false;

	for (int i = 0; i < vertexCount; i++)
		mesh.verts.push_back({ d.Get<int16_t>(vertexAt + 8 * i), d.Get<int16_t>(vertexAt + 8 * i + 2), d.Get<int16_t>(vertexAt + 8 * i + 4) });

	for (int j = 0; j < materialCount; j++)
	{
		size_t m = materialAt + 16 * j;
		Material mat;
		mat.page = d.Get<uint16_t>(m);
		mat.clut = d.Get<uint16_t>(m + 2);
		for (int k = 0; k < 4; k++)
		{
			mat.uv[k][0] = d.U8(m + 4 + 2 * k);
			mat.uv[k][1] = d.U8(m + 5 + 2 * k);
		}

		mesh.materials.push_back(mat);
	}

	int subCount = d.U8(base);
	for (int i = 0; i < subCount && !d.bad; i++)
	{
		size_t sub = subAt + 0x14 * i;
		for (int listOff = 0xc; listOff <= 0x10; listOff += 4)
		{
			uint32_t pointer = d.Get<uint32_t>(sub + listOff);
			if (!pointer)
				continue;

			size_t head = base + pointer;
			size_t table = base + d.Get<uint32_t>(head + 4);
			int groups = d.Get<int16_t>(head);

			for (int j = 0; j < groups && !d.bad; j++)
			{
				size_t entry = table + 8 * j;
				int kind = d.U8(entry), count = d.U8(entry + 3);
				int vpp = (kind & 4) ? 4 : 3;
				int size = kind == 3 || kind == 11 ? 24 : kind == 7 ? 28 : kind == 15 ? 32 : 8 + vpp * 4 + 4;

				size_t first = base + d.Get<uint32_t>(entry + 4);
				for (int k = 0; k < count && !d.bad; k++)
				{
					size_t rec = first + (size_t)k * size;
					Prim prim;
					for (int v = 0; v < vpp; v++)
					{
						for (int c = 0; c < 3; c++)
							prim.colour[v][c] = d.U8(rec + 8 + 4 * v + c);

						prim.idx.push_back(d.U8(rec + 11 + 4 * v));
					}

					prim.material = d.Get<uint16_t>(rec + 8 + vpp * 4 + (vpp == 4 ? 2 : 0)) / 16;
					mesh.prims.push_back(prim);
				}
			}
		}
	}

	return !d.bad;
}

uint16_t Bgr555(const uint8_t* rgba)
{
	if (!rgba[0] && !rgba[1] && !rgba[2] && !rgba[3])
		return 0;

	int v = (rgba[0] >> 3) | (rgba[1] >> 3) << 5 | (rgba[2] >> 3) << 10;
	return (uint16_t)(v ? v : 0x8000);
}

struct Vec { int x, y, z; };

struct Poly { std::vector<int> idx; int material; int colour[3]; };

struct CarData
{
	std::vector<Vec> verts;
	std::vector<Poly> polys;
	std::vector<Material> materials;
};

struct Wheel { int x, y, z, radius; };

// Body and wheel node numbers: wheels are the 16 vertex boxes, hull boxes and the damage copies of the body are left out.
void Classify(const Bytes& d, size_t pool, const std::vector<Node>& nodes, std::vector<int>& body, std::vector<int>& wheels)
{
	std::map<int, std::pair<int, int>> shapes;		// node -> (vertices, polygons)
	std::set<int> hulls;

	for (int n = 0; n < (int)nodes.size(); n++)
	{
		Mesh mesh;
		if (!ReadMesh(d, pool, nodes[n].poolOffset, mesh))
		{
			d.bad = false;
			continue;
		}

		shapes[n] = { (int)mesh.verts.size(), (int)mesh.prims.size() };
		if ((mesh.prims.size() == 16 && mesh.materials.size() == 8) || (mesh.verts.size() == 6 && mesh.prims.size() == 3))
			hulls.insert(n);
	}

	int main = -1;
	for (auto& s : shapes)
	{
		if (main < 0 || s.second.second > shapes[main].second)
			main = s.first;
	}

	for (auto& s : shapes)
	{
		if (s.second.first == 16 && s.second.second == 4)
			wheels.push_back(s.first);
		else if (!hulls.count(s.first) && (s.second != shapes[main] || s.first == main))
			body.push_back(s.first);
	}
}

int Round(double v)
{
	return (int)floor(v + 0.5);
}

void Unit(double x, double y, double z, int16_t out[3])
{
	double length = sqrt(x * x + y * y + z * z);
	if (length == 0.0)
		length = 1.0;

	out[0] = (int16_t)Round(4096 * x / length);
	out[1] = (int16_t)Round(4096 * y / length);
	out[2] = (int16_t)Round(4096 * z / length);
}

void PutShorts(std::vector<char>& dst, int a, int b, int c, int d)
{
	int16_t v[4] = { (int16_t)a, (int16_t)b, (int16_t)c, (int16_t)d };
	Put(dst, v, sizeof(v));
}

// MODEL file: header, vertices, point normals, polygons (FT3 / FT4), face normals
enum ModelKind { MODEL_CLEAN, MODEL_DAMAGED, MODEL_LOW };

std::vector<char> BuildModel(const CarData& car, const std::vector<std::pair<int, int>>& textureOf, ModelKind kind)
{
	int count = (int)car.verts.size();
	std::vector<char> polyBytes, faceNormals;
	std::vector<double> pointSum(count * 3, 0.0);

	for (const Poly& poly : car.polys)
	{
		const Material& mat = car.materials[poly.material];
		bool tri = poly.idx.size() == 3;
		static const int quadOrder[4] = { 0, 2, 3, 1 };	// round the edge, reversed: the engine culls polygons that turn the other way round on screen
		static const int triOrder[3] = { 0, 2, 1 };

		uint8_t head[8] = { (uint8_t)(tri ? POLY_FT3 : POLY_FT4), (uint8_t)textureOf[poly.material].first, (uint8_t)textureOf[poly.material].second, 0, 0, 0, 0, 0 };
		int n = tri ? 3 : 4;
		for (int k = 0; k < n; k++)
			head[4 + k] = (uint8_t)poly.idx[tri ? triOrder[k] : quadOrder[k]];

		Put(polyBytes, head, 8);
		for (int k = 0; k < n; k++)
		{
			int src = tri ? triOrder[k] : quadOrder[k];
			Put(polyBytes, &mat.uv[src][0], 1);
			Put(polyBytes, &mat.uv[src][1], 1);
		}

		if (tri)
		{
			uint16_t pad = 0;
			Put(polyBytes, pad);
		}

		uint8_t colour[4] = { (uint8_t)poly.colour[0], (uint8_t)poly.colour[1], (uint8_t)poly.colour[2], 0 };
		Put(polyBytes, colour, 4);

		const Vec& a = car.verts[poly.idx[0]];
		const Vec& b = car.verts[poly.idx[1]];
		const Vec& c = car.verts[poly.idx[2]];
		double vx = b.x - a.x, vy = b.y - a.y, vz = b.z - a.z, ux = c.x - a.x, uy = c.y - a.y, uz = c.z - a.z;
		double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;

		int16_t unit[3];
		Unit(nx, ny, nz, unit);
		PutShorts(faceNormals, unit[0], unit[1], unit[2], 0);

		for (int i : poly.idx)
		{
			pointSum[i * 3] += nx;
			pointSum[i * 3 + 1] += ny;
			pointSum[i * 3 + 2] += nz;
		}
	}

	// The engine copies the part before the polygons into the special slot (the polygons stay in the file buffer), so the damaged
	// and the low detail model carry only what they need: vertices, and the polygons of the low detail one.
	bool clean = kind == MODEL_CLEAN;
	int numPolys = kind == MODEL_DAMAGED ? 0 : (int)car.polys.size();

	if (kind == MODEL_DAMAGED)
		polyBytes.clear();

	if (!clean)
		faceNormals.clear();

	int vertexAt = MODEL_HEADER;
	int pointAt = clean ? vertexAt + 8 * count : vertexAt;
	int polyAt = vertexAt + 8 * count + (clean ? 8 * count : 0);
	int normalsAt = polyAt + (int)polyBytes.size();

	double radius = 0.0;
	for (const Vec& v : car.verts)
		radius = std::max(radius, sqrt((double)v.x * v.x + (double)v.y * v.y + (double)v.z * v.z));

	std::vector<char> out;
	uint16_t shapeFlags = 0, flags2 = 0, numPointNormals = (uint16_t)count, numVertices = (uint16_t)count, numPolysShort = (uint16_t)numPolys;
	int16_t instance = -1, boundingSphere = (int16_t)radius;
	uint8_t triVerts = 0, zBias = 0;
	int32_t offsets[5] = { vertexAt, polyAt, normalsAt, pointAt, 0 };

	Put(out, shapeFlags);
	Put(out, flags2);
	Put(out, instance);
	Put(out, triVerts);
	Put(out, zBias);
	Put(out, boundingSphere);
	Put(out, numPointNormals);
	Put(out, numVertices);
	Put(out, numPolysShort);
	Put(out, offsets, sizeof(offsets));

	for (const Vec& v : car.verts)
		PutShorts(out, v.x, v.y, v.z, 0);

	for (int i = 0; clean && i < count; i++)
	{
		int16_t unit[3];
		Unit(pointSum[i * 3], pointSum[i * 3 + 1], pointSum[i * 3 + 2], unit);
		PutShorts(out, unit[0], unit[1], unit[2], 0);
	}

	out.insert(out.end(), polyBytes.begin(), polyBytes.end());
	out.insert(out.end(), faceNormals.begin(), faceNormals.end());
	return out;
}

bool WriteFile(const std::string& path, const void* data, size_t size)
{
	FILE* fp = fopen(path.c_str(), "wb");
	if (!fp)
		return false;

	bool ok = fwrite(data, 1, size, fp) == size;
	fclose(fp);
	return ok;
}
}

bool Wspc_ConvertCar(const char* dobPath, const char* outFolder, std::string& log)
{
	std::vector<uint8_t> file;
	FILE* fp = fopen(dobPath, "rb");
	if (!fp)
	{
		Log(log, "ERROR: cannot open %s", dobPath);
		return false;
	}

	uint8_t buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
		file.insert(file.end(), buf, buf + n);

	fclose(fp);

	Bytes d{ file.data(), file.size() };
	std::vector<Chunk> chunks;
	ParseChunks(d, 0, file.size(), chunks);

	std::vector<Chunk> pages, poolChunks, nodeChunks;
	for (const Chunk& c : chunks)
	{
		if (c.tag == 3)
			pages.push_back(c);
		else if (c.tag == 5)
			poolChunks.push_back(c);
		else if (c.tag == 6)
			nodeChunks.push_back(c);
	}

	if (pages.empty() || poolChunks.empty() || nodeChunks.empty())
	{
		Log(log, "ERROR: %s is not a vehicle file", dobPath);
		return false;
	}

	size_t pool = poolChunks[0].off + 8;
	std::vector<Node> nodes;
	for (size_t i = 0; i < (nodeChunks[0].size - 8) / 0x38; i++)
	{
		size_t at = nodeChunks[0].off + 8 + i * 0x38;
		Node node;
		node.poolOffset = d.Get<int32_t>(at + 4);
		for (int k = 0; k < 3; k++)
			node.pos[k] = d.Get<int32_t>(at + 8 + 4 * k);

		node.parent = d.Get<int32_t>(at + 48);
		nodes.push_back(node);
	}

	std::vector<int> body, wheels;
	Classify(d, pool, nodes, body, wheels);

	CarData car;
	for (int number : body)
	{
		Mesh mesh;
		d.bad = false;
		if (!ReadMesh(d, pool, nodes[number].poolOffset, mesh))
		{
			Log(log, "ERROR: node %d of %s is damaged", number, dobPath);
			return false;
		}

		int firstVert = (int)car.verts.size(), materialBase = (int)car.materials.size();
		car.materials.insert(car.materials.end(), mesh.materials.begin(), mesh.materials.end());
		for (auto& v : mesh.verts)
			car.verts.push_back({ Round(-(v[0] + nodes[number].pos[0]) * WSPCDLC_CAR_SCALE), Round((v[1] + nodes[number].pos[1]) * WSPCDLC_CAR_SCALE), Round(-(v[2] + nodes[number].pos[2]) * WSPCDLC_CAR_SCALE) });

		for (const Prim& prim : mesh.prims)
		{
			bool ok = prim.material < (int)mesh.materials.size();
			for (int i : prim.idx)
				ok = ok && i < (int)mesh.verts.size();

			if (!ok)
				continue;

			Poly poly;
			poly.material = materialBase + prim.material;
			for (int i : prim.idx)
				poly.idx.push_back(firstVert + i);

			for (int c = 0; c < 3; c++)
			{
				int sum = 0;
				for (size_t v = 0; v < prim.idx.size(); v++)
					sum += prim.colour[v][c];

				poly.colour[c] = sum / (int)prim.idx.size();
			}

			car.polys.push_back(poly);
		}
	}

	// unused vertices (hull leftovers) are dropped and the polygons renumbered
	std::map<int, int> renumber;
	std::set<int> used;
	for (const Poly& poly : car.polys)
		used.insert(poly.idx.begin(), poly.idx.end());

	std::vector<Vec> kept;
	for (int i : used)
	{
		renumber[i] = (int)kept.size();
		kept.push_back(car.verts[i]);
	}

	for (Poly& poly : car.polys)
	{
		for (int& i : poly.idx)
			i = renumber[i];
	}

	car.verts = kept;
	if (car.verts.empty() || car.polys.empty())
	{
		Log(log, "ERROR: %s has no body polygons", dobPath);
		return false;
	}

	if ((int)car.verts.size() > MAX_VERTS)
	{
		Log(log, "ERROR: %s has %d vertices, polygons address at most %d", dobPath, (int)car.verts.size(), MAX_VERTS);
		return false;
	}

	// the model is centred on its bounding box like the Redriver cars (wheel positions follow in CAR.INFO)
	int lo[3] = { car.verts[0].x, car.verts[0].y, car.verts[0].z }, hi[3] = { lo[0], lo[1], lo[2] };
	for (const Vec& v : car.verts)
	{
		int c[3] = { v.x, v.y, v.z };
		for (int k = 0; k < 3; k++)
		{
			lo[k] = std::min(lo[k], c[k]);
			hi[k] = std::max(hi[k], c[k]);
		}
	}

	int centre[3];
	for (int k = 0; k < 3; k++)
		centre[k] = (int)floor((lo[k] + hi[k]) / 2.0);

	for (Vec& v : car.verts)
	{
		v.x -= centre[0];
		v.y -= centre[1];
		v.z -= centre[2];
	}

	// texture pages and palettes: every used (page, palette) pair becomes texture id n of output page 0 / 1
	std::set<std::pair<int, int>> usedPairs;
	for (const Poly& poly : car.polys)
		usedPairs.insert({ car.materials[poly.material].page, car.materials[poly.material].clut });

	std::vector<int> pageOrder;
	for (auto& pr : usedPairs)
	{
		if (pageOrder.empty() || pageOrder.back() != pr.first)
			pageOrder.push_back(pr.first);
	}

	if ((int)pageOrder.size() > MAX_PAGES || (int)usedPairs.size() > MAX_PALETTES)
	{
		Log(log, "ERROR: %s uses %d texture pages and %d palettes (a car holds %d and %d)", dobPath, (int)pageOrder.size(), (int)usedPairs.size(), MAX_PAGES, MAX_PALETTES);
		return false;
	}

	std::map<std::pair<int, int>, std::pair<int, int>> lookup;
	std::vector<std::vector<int>> palettes(pageOrder.size());
	for (auto& pr : usedPairs)
	{
		int outPage = (int)(std::find(pageOrder.begin(), pageOrder.end(), pr.first) - pageOrder.begin());
		lookup[pr] = { outPage, (int)palettes[outPage].size() };
		palettes[outPage].push_back(pr.second);
	}

	std::vector<char> texture;
	uint32_t pageCount = (uint32_t)pageOrder.size();
	Put(texture, pageCount);
	for (size_t outPage = 0; outPage < pageOrder.size(); outPage++)
	{
		int page = pageOrder[outPage];
		if (page >= (int)pages.size())
		{
			Log(log, "ERROR: %s uses missing texture page %d", dobPath, page);
			return false;
		}

		size_t start = pages[page].off + 8, end = pages[page].off + pages[page].size;
		size_t paletteStart = start + 4 + PAGE_BYTES;
		uint32_t paletteCount = (uint32_t)palettes[outPage].size();
		Put(texture, paletteCount);

		for (int clut : palettes[outPage])
		{
			size_t at = paletteStart + (size_t)clut * 64;
			if (at + 64 > end)
				at = paletteStart;

			for (int k = 0; k < 16; k++)
			{
				uint8_t rgba[4];
				for (int c = 0; c < 4; c++)
					rgba[c] = d.U8(at + 4 * k + c);

				uint16_t colour = Bgr555(rgba);
				Put(texture, colour);
			}
		}

		for (int i = 0; i < PAGE_BYTES; i++)
			texture.push_back((char)d.U8(start + 4 + i));
	}

	if (d.bad)
	{
		Log(log, "ERROR: %s is truncated", dobPath);
		return false;
	}

	std::vector<std::pair<int, int>> textureOf(car.materials.size(), { 0, 0 });
	for (const Poly& poly : car.polys)
	{
		const Material& mat = car.materials[poly.material];
		textureOf[poly.material] = lookup[{ mat.page, mat.clut }];
	}


	std::vector<Wheel> wheelInfo;
	for (int number : wheels)
	{
		Mesh mesh;
		d.bad = false;
		if (!ReadMesh(d, pool, nodes[number].poolOffset, mesh) || mesh.verts.empty())
			continue;

		double wlo[3] = { 1e30, 1e30, 1e30 }, whi[3] = { -1e30, -1e30, -1e30 };
		for (auto& v : mesh.verts)
		{
			for (int k = 0; k < 3; k++)
			{
				double scaled = (v[k] + nodes[number].pos[k]) * WSPCDLC_CAR_SCALE;
				wlo[k] = std::min(wlo[k], scaled);
				whi[k] = std::max(whi[k], scaled);
			}
		}

		wheelInfo.push_back({ Round(-(wlo[0] + whi[0]) / 2) - centre[0], Round((wlo[1] + whi[1]) / 2) - centre[1], Round(-(wlo[2] + whi[2]) / 2) - centre[2], Round((whi[1] - wlo[1]) / 2) });
	}

	std::string info;
	char line[160];
	snprintf(line, sizeof(line), "vertices=%d\npolys=%d\npages=%d\npalettes=%d\n", (int)car.verts.size(), (int)car.polys.size(), (int)pageOrder.size(), (int)usedPairs.size());
	info += line;
	for (size_t i = 0; i < wheelInfo.size(); i++)
	{
		snprintf(line, sizeof(line), "wheel%d=x%d y%d z%d radius%d\n", (int)i, wheelInfo[i].x, wheelInfo[i].y, wheelInfo[i].z, wheelInfo[i].radius);
		info += line;
	}

	MakeFolder(outFolder);
	std::string folder = std::string(outFolder) + "\\";
	bool ok = true;
	const char* modelNames[3] = { "CLEAN.MDL", "DAMAGED.MDL", "LOW.MDL" };
	for (int kind = 0; kind < 3; kind++)
	{
		std::vector<char> model = BuildModel(car, textureOf, (ModelKind)kind);
		ok = ok && WriteFile(folder + modelNames[kind], model.data(), model.size());
	}

	ok = ok && WriteFile(folder + "TEXTURE.BIN", texture.data(), texture.size());
	ok = ok && WriteFile(folder + "CAR.INFO", info.data(), info.size());

	if (!ok)
		Log(log, "ERROR: cannot write the files of %s", outFolder);
	else
		Log(log, "%s: %d vertices, %d polygons, %d palettes", dobPath, (int)car.verts.size(), (int)car.polys.size(), (int)usedPairs.size());

	return ok;
}
