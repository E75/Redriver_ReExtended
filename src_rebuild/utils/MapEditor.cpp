// In-game map editor, part 1 (plans/map-editor.md): free camera, aim at a prop, select it, delete / undo.
// Where to find what: MapEditor_Draw (called every frame from DrawDebugOverlays) = pick + HUD + selection box,
// MapEditor_Key (called from GameKeys) = camera keys and edit keys, PickObject = ray against the cell objects.
// Active with the option -editor or F4. The game keeps running; the free camera of DebugOverlay.cpp flies.

#include "driver2.h"

#include "C/map.h"
#include "C/mission.h"
#include "C/cell.h"
#include "C/models.h"
#include "C/draw.h"
#include "C/pres.h"
#include "C/camera.h"
#include "C/system.h"
#include "C/main.h"
#include "C/cars.h"
#include "C/players.h"
#include "C/handling.h"
#include "C/dr2roads.h"

#include "PsyX/PsyX_public.h"

#include <SDL_mouse.h>
#include <SDL_scancode.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

int gEditor = 0;
int gEditorTest = 0;			// -editortest: deletes the aimed object at frame 60, undoes it at frame 140 (for -shot runs)
char* gModelNamesCopy = NULL;		// main.c: copy of the MODELNAMES lump (strings, one per model number)

extern int g_FreeCameraEnabled, g_FreeCameraControls, g_FreeCameraSpeed;
extern VECTOR g_FreeCameraPosition, g_FreeCameraVelocity;
extern SVECTOR g_FreeCameraRotation;
extern void FreeCameraMouseHandler(int x, int y, int dx, int dy);
extern void PsyX_TakeScreenshot();
extern void Debug_AddLine(VECTOR& pointA, VECTOR& pointB, CVECTOR& color);

namespace
{

const int kPickRange = 30000;			// units along the view direction
const int kMaxUndo = 64;

struct EditorObject
{
	PACKED_CELL_OBJECT*	packed;
	CELL_OBJECT			co;				// unpacked, world position
	int					cellX, cellZ;
};

// objects the editor added (moved and copied props): the cell iterator hands them out after the normal list of their cell
struct Extra
{
	PACKED_CELL_OBJECT	po;				// first member: a PACKED_CELL_OBJECT* of the iterator casts back to Extra*
	int					cx, cz;
};

const int kMaxExtra = 4096;
Extra			gExtra[kMaxExtra];
int				gNumExtra = 0;

enum UndoOp { OP_DELETE_ORIG, OP_ADD_EXTRA, OP_CHANGE_EXTRA, OP_REMOVE_BOX };

struct UndoEntry
{
	UndoOp				op;
	bool				chain;			// also undo the entry below (moving an original = delete + add)
	PACKED_CELL_OBJECT*	packed;			// OP_DELETE_ORIG
	PACKED_CELL_OBJECT	old;
	int					extra;			// OP_ADD_EXTRA, OP_CHANGE_EXTRA
	Extra				oldExtra;
	int					boxType, boxIndex;	// OP_REMOVE_BOX: model type and slot the collision box had
	COLLISION_PACKET	box;
};

// what the edit file holds: deleted originals (raw packed data), collision boxes taken out of a model, added props are gExtra
const int kMaxRemoved = 8192, kMaxBoxRemoved = 1024;
PACKED_CELL_OBJECT	gRemoved[kMaxRemoved];
int					gNumRemoved = 0;

struct BoxRemoval
{
	int					model;
	COLLISION_PACKET	box;
};

BoxRemoval	gBoxRemoved[kMaxBoxRemoved];
int			gNumBoxRemoved = 0;
char		gSaveMsg[80] = "";
VECTOR_NOPAD gPending[4096];		// world position of the added props read from the file until their cells are known
bool		gAddsPending = false;
int			gAppliedRemoved = 0, gAppliedBoxes = 0;	// how many of the file's removals found their object / box on load

const int kSteps[] = { 10, 50, 250, 1024, 2048 };

EditorObject	gHover, gSelected;
bool			gHasHover = false, gHasSelected = false;
UndoEntry		gUndo[kMaxUndo];
int				gUndoCount = 0;
int				gDeletedCount = 0;
bool			gMouseWasDown = false;
int				gStepIndex = 2;
bool			gSnap = true;
int				gAddModel = 1;
bool			gHasClip = false;
bool			gShowBoxes = false;
bool			gGotoPlayer = false;		// HOME: view back over the player
int				gPanelRight = 0, gPanelBottom = 0;	// PSX screen rectangle of the panel (clicks there never select a prop)
int				gBackX, gBackY, gBackW, gBackH;		// PSX screen rectangle of the BACK button
bool			gHelp = false;				// F1: list of all keys beside the panel
bool			gTopDown = true;			// top-down view with a free mouse pointer, TAB switches to free look
int				gViewX, gViewZ, gViewH = 3500;	// top-down camera: ground position and height above the ground
int				gPanKeys = 0;				// 1 up, 2 down, 4 left, 8 right, 16 lower, 32 higher
bool			gDragging = false;
int				gGrabX, gGrabZ;				// selected prop minus the ground point under the pointer when the drag started
CELL_OBJECT		gDragCo;
int				gBoxesInView = 0;
int				gTestType = 0, gTestCounts[3];
int				gBoxSel = 0;				// collision box of the selected prop's model that J picked
CELL_OBJECT		gClip;

struct ModelBox
{
	MODEL*	model;
	int		min[3], max[3];
};

ModelBox gBoxes[MAX_MODEL_SLOTS];

bool IsDeleted(const PACKED_CELL_OBJECT* p)
{
	return p->value == 0xffff && (p->pos.vy & 1);
}

// bounding box of the vertices of a model (cached per model pointer, the pointer changes when an area streams in)
const ModelBox* GetBox(int type)
{
	if (type < 0 || type >= MAX_MODEL_SLOTS)
		return NULL;

	MODEL* model = modelpointers[type];
	if (!model || model->num_vertices == 0)
		return NULL;

	ModelBox& box = gBoxes[type];
	if (box.model == model)
		return &box;

	SVECTOR* v = GET_MODEL_DATA(SVECTOR, model, vertices);
	if (!v)
		return NULL;

	box.model = model;
	for (int k = 0; k < 3; k++)
		box.min[k] = box.max[k] = (&v[0].vx)[k];

	for (int i = 1; i < model->num_vertices; i++)
	{
		for (int k = 0; k < 3; k++)
		{
			const int c = (&v[i].vx)[k];
			box.min[k] = box.min[k] < c ? box.min[k] : c;
			box.max[k] = box.max[k] > c ? box.max[k] : c;
		}
	}

	return &box;
}

// local = transpose(M) * world, M = matrixtable[yang] (fixed point 4096)
void ToLocal(const CELL_OBJECT& co, const double w[3], double out[3])
{
	const MATRIX* m = (const MATRIX*)&matrixtable[co.yang];
	for (int j = 0; j < 3; j++)
		out[j] = (m->m[0][j] * w[0] + m->m[1][j] * w[1] + m->m[2][j] * w[2]) / 4096.0;
}

// slab test of the camera ray against the model box of one object; returns the distance along the ray or -1
double RayHit(const CELL_OBJECT& co, const ModelBox& box, const double origin[3], const double dir[3])
{
	const double rel[3] = { origin[0] - co.pos.vx, origin[1] - co.pos.vy, origin[2] - co.pos.vz };
	double o[3], d[3];
	ToLocal(co, rel, o);
	ToLocal(co, dir, d);

	double tNear = 0, tFar = kPickRange;
	for (int k = 0; k < 3; k++)
	{
		if (fabs(d[k]) < 1e-9)
		{
			if (o[k] < box.min[k] || o[k] > box.max[k])
				return -1;

			continue;
		}

		double t1 = (box.min[k] - o[k]) / d[k], t2 = (box.max[k] - o[k]) / d[k];
		if (t1 > t2)
		{
			const double t = t1;
			t1 = t2;
			t2 = t;
		}

		tNear = tNear > t1 ? tNear : t1;
		tFar = tFar < t2 ? tFar : t2;
		if (tNear > tFar)
			return -1;
	}

	return tNear;
}

// nearest object under the crosshair, looks at the cells along the ray and their neighbours
// mouse pointer in PSX screen units (widescreen aware); scripted runs hold it at the screen centre
void MousePsx(double& px, double& py)
{
	int w, h, mx, my;
	PsyX_GetScreenSize(&w, &h);
	SDL_GetMouseState(&mx, &my);
	if (gEditorTest)
	{
		mx = w / 2;
		my = h / 2;
	}

	RECT16 vp;
	PsyX_GetPSXWidescreenMappedViewport(&vp);
	px = vp.x + (double)mx / w * (vp.w - vp.x);
	py = vp.y + (double)my / h * vp.h;
}

// world ray of the view: through the mouse pointer (top-down) or through the screen centre (free look)
void ViewRay(double origin[3], double dir[3])
{
	origin[0] = camera_position.vx;
	origin[1] = camera_position.vy;
	origin[2] = camera_position.vz;

	double f[3], r[3], d[3];
	for (int k = 0; k < 3; k++)
	{
		r[k] = inv_camera_matrix.m[0][k] / 4096.0;
		d[k] = inv_camera_matrix.m[1][k] / 4096.0;
		f[k] = inv_camera_matrix.m[2][k] / 4096.0;
		dir[k] = f[k];
	}

	if (!gTopDown)
		return;

	double px, py;
	MousePsx(px, py);

	RECT16 vp;
	PsyX_GetPSXWidescreenMappedViewport(&vp);
	// the PSX projection: screen = centre + view x,y * scr_z / view z
	const double focal = scr_z, cx = (vp.x + vp.w) / 2.0, cy = vp.y + vp.h / 2.0;
	const double a = (px - cx) / focal, b = (py - cy) / focal;

	double len = 0;
	for (int k = 0; k < 3; k++)
	{
		dir[k] = f[k] + a * r[k] + b * d[k];
		len += dir[k] * dir[k];
	}

	len = sqrt(len);
	for (int k = 0; k < 3; k++)
		dir[k] /= len;
}

bool PickObject(EditorObject& out)
{
	double origin[3], dir[3];
	ViewRay(origin, dir);

	double best = 1e18;
	bool found = false;

	int visited[2048];
	int numVisited = 0;
	PACKED_CELL_OBJECT* seen[1024];
	int numSeen = 0;

	for (int t = 0; t <= kPickRange; t += MAP_CELL_SIZE / 2)
	{
		const int cx = (int)((origin[0] + dir[0] * t + units_across_halved) / MAP_CELL_SIZE);
		const int cz = (int)((origin[2] + dir[2] * t + units_down_halved) / MAP_CELL_SIZE);

		for (int dz = -1; dz <= 1; dz++)
		{
			for (int dx = -1; dx <= 1; dx++)
			{
				const int x = cx + dx, z = cz + dz;
				if (x < 0 || z < 0 || x >= cells_across || z >= cells_down)
					continue;

				const int id = z * cells_across + x;
				bool done = false;
				for (int i = 0; i < numVisited && !done; i++)
					done = visited[i] == id;

				if (done || numVisited >= 2048)
					continue;

				visited[numVisited++] = id;

				CELL_ITERATOR ci;
				for (PACKED_CELL_OBJECT* p = GetFirstPackedCop(x, z, &ci, 0); p; p = GetNextPackedCop(&ci))
				{
					bool dup = false;
					for (int i = 0; i < numSeen && !dup; i++)
						dup = seen[i] == p;

					if (dup || numSeen >= 1024)
						continue;

					seen[numSeen++] = p;

					CELL_OBJECT co;
					QuickUnpackCellObject(p, &ci.nearCell, &co);

					const ModelBox* box = GetBox(co.type);
					if (!box)
						continue;

					const double hit = RayHit(co, *box, origin, dir);
					if (hit >= 0 && hit < best)
					{
						best = hit;
						out.packed = p;
						out.co = co;
						out.cellX = x;
						out.cellZ = z;
						found = true;
					}
				}
			}
		}
	}

	return found;
}

void DrawBox(const EditorObject& obj, CVECTOR& color)
{
	const ModelBox* box = GetBox(obj.co.type);
	if (!box)
		return;

	const MATRIX* m = (const MATRIX*)&matrixtable[obj.co.yang];
	VECTOR corner[8];

	for (int i = 0; i < 8; i++)
	{
		const int l[3] = { (i & 1) ? box->max[0] : box->min[0], (i & 2) ? box->max[1] : box->min[1], (i & 4) ? box->max[2] : box->min[2] };

		// world y is down; Debug_AddLine wants y up
		corner[i].vx = obj.co.pos.vx + ((m->m[0][0] * l[0] + m->m[0][1] * l[1] + m->m[0][2] * l[2]) >> 12);
		corner[i].vy = -(obj.co.pos.vy + ((m->m[1][0] * l[0] + m->m[1][1] * l[1] + m->m[1][2] * l[2]) >> 12));
		corner[i].vz = obj.co.pos.vz + ((m->m[2][0] * l[0] + m->m[2][1] * l[1] + m->m[2][2] * l[2]) >> 12);
	}

	for (int i = 0; i < 8; i++)
	{
		for (int bit = 1; bit <= 4; bit <<= 1)
		{
			if (!(i & bit))
				Debug_AddLine(corner[i], corner[i | bit], color);
		}
	}
}

const char* ModelName(int type)
{
	const char* p = gModelNamesCopy;
	if (!p)
		return "?";

	for (int i = 0; i < type; i++)
		p += strlen(p) + 1;

	return p;
}

bool IsExtra(const PACKED_CELL_OBJECT* p)
{
	return (const char*)p >= (const char*)gExtra && (const char*)p < (const char*)(gExtra + kMaxExtra);
}

void MarkDeleted(PACKED_CELL_OBJECT* p)
{
	p->value = 0xffff;
	p->pos.vy |= 1;
}

// inverse of QuickUnpackCellObject
PACKED_CELL_OBJECT Pack(const CELL_OBJECT& co)
{
	PACKED_CELL_OBJECT p;
	p.pos.vx = (unsigned short)co.pos.vx;
	p.pos.vz = (unsigned short)co.pos.vz;
	p.pos.vy = (unsigned short)((co.pos.vy << 1) | ((co.type >> 10) & 1));
	p.value = (unsigned short)((co.type << 6) | co.yang);
	return p;
}

void SetObject(EditorObject& o, PACKED_CELL_OBJECT* p, int cx, int cz)
{
	XZPAIR near;
	near.x = (cx - (cells_across / 2)) * MAP_CELL_SIZE;
	near.z = (cz - (cells_down / 2)) * MAP_CELL_SIZE;

	o.packed = p;
	o.cellX = cx;
	o.cellZ = cz;
	QuickUnpackCellObject(p, &near, &o.co);
}

void SelectExtra(int i)
{
	gHasSelected = true;
	SetObject(gSelected, &gExtra[i].po, gExtra[i].cx, gExtra[i].cz);
}

// collision boxes of a model (shared by all its instances); n = box count
COLLISION_PACKET* BoxList(int type, int& n)
{
	n = 0;
	MODEL* model = (type >= 0 && type < MAX_MODEL_SLOTS) ? modelpointers[type] : NULL;
	if (!model || model->collision_block <= 0)
		return NULL;

	n = *GET_MODEL_DATA(int, model, collision_block);
	return GET_MODEL_DATA_OFS(COLLISION_PACKET, model, collision_block, sizeof(int));
}

// puts a removed box back at slot idx (the list was shifted down one slot, its old last slot is free)
void InsertBox(int type, int idx, const COLLISION_PACKET& box)
{
	int n;
	COLLISION_PACKET* list = BoxList(type, n);
	if (!list || idx < 0 || idx > n)
		return;

	memmove(&list[idx + 1], &list[idx], (n - idx) * sizeof(COLLISION_PACKET));
	list[idx] = box;
	*GET_MODEL_DATA(int, modelpointers[type], collision_block) = n + 1;
}

bool SameObject(const PACKED_CELL_OBJECT& a, const PACKED_CELL_OBJECT& b)
{
	return memcmp(&a, &b, sizeof(a)) == 0;
}

void ForgetRemoved(const PACKED_CELL_OBJECT& o)
{
	for (int i = gNumRemoved - 1; i >= 0; i--)
	{
		if (SameObject(gRemoved[i], o))
		{
			gRemoved[i] = gRemoved[--gNumRemoved];
			return;
		}
	}
}

void ForgetBoxRemoved(int model, const COLLISION_PACKET& box)
{
	for (int i = gNumBoxRemoved - 1; i >= 0; i--)
	{
		if (gBoxRemoved[i].model == model && memcmp(&gBoxRemoved[i].box, &box, sizeof(box)) == 0)
		{
			gBoxRemoved[i] = gBoxRemoved[--gNumBoxRemoved];
			return;
		}
	}
}

UndoEntry* PushUndo(UndoOp op, bool chain)
{
	if (gUndoCount >= kMaxUndo)
	{
		printf("editor: undo list is full\n");
		return NULL;
	}

	UndoEntry* u = &gUndo[gUndoCount++];
	u->op = op;
	u->chain = chain;
	return u;
}

int AddExtra(const CELL_OBJECT& co, bool chain)
{
	int i = 0;
	while (i < gNumExtra && !IsDeleted(&gExtra[i].po))
		i++;

	UndoEntry* u = i < kMaxExtra ? PushUndo(OP_ADD_EXTRA, chain) : NULL;
	if (!u)
		return -1;

	if (i == gNumExtra)
		gNumExtra++;

	gExtra[i].po = Pack(co);
	gExtra[i].cx = (co.pos.vx + units_across_halved) / MAP_CELL_SIZE;
	gExtra[i].cz = (co.pos.vz + units_down_halved) / MAP_CELL_SIZE;
	u->extra = i;
	return i;
}

void DeleteSelected()
{
	if (!gHasSelected || IsDeleted(gSelected.packed))
		return;

	UndoEntry* u = PushUndo(IsExtra(gSelected.packed) ? OP_CHANGE_EXTRA : OP_DELETE_ORIG, false);
	if (!u)
		return;

	if (u->op == OP_CHANGE_EXTRA)
	{
		u->extra = (int)((Extra*)gSelected.packed - gExtra);
		u->oldExtra = gExtra[u->extra];
	}
	else
	{
		u->packed = gSelected.packed;
		u->old = *gSelected.packed;
		gDeletedCount++;
		if (gNumRemoved < kMaxRemoved)
			gRemoved[gNumRemoved++] = u->old;
	}

	MarkDeleted(gSelected.packed);
	printf("editor: deleted %s at %d %d %d yang %d\n", ModelName(gSelected.co.type), gSelected.co.pos.vx, gSelected.co.pos.vy, gSelected.co.pos.vz, gSelected.co.yang);
	gHasSelected = false;
}

// changes the selected object: an added one is edited in place, an original is deleted and replaced by an added one
void ChangeSelected(const CELL_OBJECT& n)
{
	if (!gHasSelected)
		return;

	if (IsExtra(gSelected.packed))
	{
		UndoEntry* u = PushUndo(OP_CHANGE_EXTRA, false);
		if (!u)
			return;

		const int i = (int)((Extra*)gSelected.packed - gExtra);
		u->extra = i;
		u->oldExtra = gExtra[i];

		gExtra[i].po = Pack(n);
		gExtra[i].cx = (n.pos.vx + units_across_halved) / MAP_CELL_SIZE;
		gExtra[i].cz = (n.pos.vz + units_down_halved) / MAP_CELL_SIZE;
		SelectExtra(i);
	}
	else if (gUndoCount + 2 <= kMaxUndo)
	{
		DeleteSelected();

		const int i = AddExtra(n, true);
		if (i >= 0)
			SelectExtra(i);
	}
}

void Undo()
{
	bool again;

	do
	{
		if (gUndoCount == 0)
			return;

		UndoEntry& u = gUndo[--gUndoCount];
		again = u.chain;

		if (u.op == OP_DELETE_ORIG)
		{
			// a region that streamed out and in again no longer holds the deleted marker: leave it alone
			if (IsDeleted(u.packed))
			{
				*u.packed = u.old;
				gDeletedCount--;
			}

			ForgetRemoved(u.old);
		}
		else if (u.op == OP_REMOVE_BOX)
		{
			InsertBox(u.boxType, u.boxIndex, u.box);
			ForgetBoxRemoved(u.boxType, u.box);
		}
		else if (u.op == OP_ADD_EXTRA)
			MarkDeleted(&gExtra[u.extra].po);
		else
			gExtra[u.extra] = u.oldExtra;
	} while (again);

	gHasSelected = false;
}

// camera ray on the horizontal plane at world height y (y is down in world space)
bool AimOnPlane(int y, int& x, int& z)
{
	double origin[3], dir[3];
	ViewRay(origin, dir);
	if (fabs(dir[1]) < 0.02)
		return false;

	const double t = (y - origin[1]) / dir[1];
	if (t <= 0 || t > 3.0 * kPickRange)
		return false;

	x = (int)(origin[0] + dir[0] * t);
	z = (int)(origin[2] + dir[2] * t);
	return true;
}

// adds a copy of `co` at the aim point; its height decides the plane, snapping keeps the grid phase of `co`
void PlaceAtAim(CELL_OBJECT co)
{
	int x, z;
	if (!AimOnPlane(co.pos.vy, x, z))
		return;

	if (gSnap)
	{
		x = co.pos.vx + (int)floor((x - co.pos.vx) / (double)MAP_CELL_SIZE + 0.5) * MAP_CELL_SIZE;
		z = co.pos.vz + (int)floor((z - co.pos.vz) / (double)MAP_CELL_SIZE + 0.5) * MAP_CELL_SIZE;
	}

	co.pos.vx = x;
	co.pos.vz = z;

	const int i = AddExtra(co, false);
	if (i >= 0)
		SelectExtra(i);
}

// dominant world axis of a view vector: (1,0), (-1,0), (0,1) or (0,-1)
void AxisOf(int vx, int vz, int& ax, int& az)
{
	ax = az = 0;
	if (abs(vx) > abs(vz))
		ax = vx > 0 ? 1 : -1;
	else
		az = vz > 0 ? 1 : -1;
}

// moves the selection by steps along the world axes nearest to the camera's right / forward, up is a step of height
void MoveSelected(int right, int forward, int up)
{
	if (!gHasSelected)
		return;

	int fx, fz, rx, rz;
	if (gTopDown)
		AxisOf(-inv_camera_matrix.m[1][0], -inv_camera_matrix.m[1][2], fx, fz);
	else
		AxisOf(inv_camera_matrix.m[2][0], inv_camera_matrix.m[2][2], fx, fz);
	AxisOf(inv_camera_matrix.m[0][0], inv_camera_matrix.m[0][2], rx, rz);

	CELL_OBJECT n = gSelected.co;
	const int step = kSteps[gStepIndex];
	n.pos.vx += (right * rx + forward * fx) * step;
	n.pos.vz += (right * rz + forward * fz) * step;
	n.pos.vy -= up * step;
	ChangeSelected(n);
}

void RotateSelected(int delta)
{
	if (!gHasSelected)
		return;

	CELL_OBJECT n = gSelected.co;
	n.yang = (n.yang + delta) & 63;
	ChangeSelected(n);
}

void CycleModel(int dir)
{
	for (int i = 0; i < MAX_MODEL_SLOTS; i++)
	{
		gAddModel = (gAddModel + dir + MAX_MODEL_SLOTS) % MAX_MODEL_SLOTS;
		if (GetBox(gAddModel))
			return;
	}
}

// new object of the chosen model at the aim point, at the height of the selection (or the clipboard)
void AddModelAtAim()
{
	CELL_OBJECT n = {};
	if (gHasSelected)
		n = gSelected.co;
	else if (gHasClip)
		n = gClip;

	n.type = gAddModel;
	n.yang = 0;
	PlaceAtAim(n);
}

// takes box idx out of the model; undo = false for bulk cuts from the command port (the undo list holds only 64)
bool RemoveBoxAt(int type, int idx, bool undo)
{
	int n;
	COLLISION_PACKET* list = BoxList(type, n);
	if (!list || idx < 0 || idx >= n)
		return false;

	if (undo)
	{
		UndoEntry* u = PushUndo(OP_REMOVE_BOX, false);
		if (!u)
			return false;

		u->boxType = type;
		u->boxIndex = idx;
		u->box = list[idx];
	}

	if (gNumBoxRemoved >= kMaxBoxRemoved)
		return false;

	gBoxRemoved[gNumBoxRemoved].model = type;
	gBoxRemoved[gNumBoxRemoved++].box = list[idx];

	memmove(&list[idx], &list[idx + 1], (n - idx - 1) * sizeof(COLLISION_PACKET));
	*GET_MODEL_DATA(int, modelpointers[type], collision_block) = n - 1;
	return true;
}

void RemoveSelectedBox()
{
	if (gHasSelected && RemoveBoxAt(gSelected.co.type, gBoxSel, true))
		gBoxSel = 0;
}

// the footprint test of objcoll.c (CheckScenaryCollisions) as lines: center from the rotated box offset, extents (size + 10) / 2
void DrawCollisionBox(const CELL_OBJECT& co, const COLLISION_PACKET& c, CVECTOR& color)
{
	const MATRIX2* mat = &matrixtable[-co.yang & 63];
	const int theta = (co.yang + c.yang) * 64;
	const int cs = RCOS(theta), sn = RSIN(theta);

	const int midX = co.pos.vx + FIXEDH(c.xpos * mat->m[0][0] + c.zpos * mat->m[2][0]);
	const int midZ = co.pos.vz + FIXEDH(c.xpos * mat->m[0][2] + c.zpos * mat->m[2][2]);
	const int hx = (c.xsize + 10) / 2, hz = (c.zsize + 10) / 2;
	const int midY = co.pos.vy + c.ypos;

	VECTOR p[8];
	for (int i = 0; i < 8; i++)
	{
		const int sx = (i & 1) ? hx : -hx, sz = (i & 2) ? hz : -hz;

		p[i].vx = midX + ((sx * cs + sz * sn) >> 12);
		p[i].vz = midZ + ((sz * cs - sx * sn) >> 12);
		p[i].vy = -(midY + ((i & 4) ? c.ysize / 2 : -c.ysize / 2));		// y is down in the world, Debug_AddLine wants it up
	}

	for (int i = 0; i < 8; i++)
	{
		for (int bit = 1; bit <= 4; bit <<= 1)
		{
			if (!(i & bit))
				Debug_AddLine(p[i], p[i | bit], color);
		}
	}
}

// boxes of the props around the camera (red), the boxes of the selected prop in yellow, the one J picked in white
void DrawCollisionBoxes()
{
	CVECTOR red = { 255, 40, 40 }, yellow = { 255, 220, 0 }, white = { 255, 255, 255 };
	const int range = 9000;

	gBoxesInView = 0;
	if (gShowBoxes)
	{
		const int cx = (camera_position.vx + units_across_halved) / MAP_CELL_SIZE;
		const int cz = (camera_position.vz + units_down_halved) / MAP_CELL_SIZE;
		const int r = range / MAP_CELL_SIZE + 1;

		for (int z = cz - r; z <= cz + r; z++)
		{
			for (int x = cx - r; x <= cx + r; x++)
			{
				if (x < 0 || z < 0 || x >= cells_across || z >= cells_down)
					continue;

				CELL_ITERATOR ci;
				for (PACKED_CELL_OBJECT* p = GetFirstPackedCop(x, z, &ci, 0); p; p = GetNextPackedCop(&ci))
				{
					CELL_OBJECT co;
					QuickUnpackCellObject(p, &ci.nearCell, &co);

					const int dx = co.pos.vx - camera_position.vx, dz = co.pos.vz - camera_position.vz;
					// the line list is limited: only props in front of the camera
					if (dx * dx + dz * dz > range * range || (long long)dx * inv_camera_matrix.m[2][0] + (long long)dz * inv_camera_matrix.m[2][2] < 0)
						continue;

					int n;
					COLLISION_PACKET* list = BoxList(co.type, n);
					for (int i = 0; i < n; i++)
					{
						DrawCollisionBox(co, list[i], red);
						gBoxesInView++;
					}
				}
			}
		}
	}

	if (gHasSelected)
	{
		int n;
		COLLISION_PACKET* list = BoxList(gSelected.co.type, n);
		for (int i = 0; i < n; i++)
			DrawCollisionBox(gSelected.co, list[i], i == gBoxSel ? white : yellow);
	}
}

// one line per collision box next to the model's own vertex box: a box far bigger than the model is an invisible wall
void WriteBoxReport()
{
	FILE* f = fopen("MAPEDITOR_BOXES.txt", "w");
	if (!f)
		return;

	for (int t = 0; t < MAX_MODEL_SLOTS; t++)
	{
		int n;
		COLLISION_PACKET* list = BoxList(t, n);
		const ModelBox* mb = GetBox(t);
		if (!n || !mb)
			continue;

		for (int i = 0; i < n; i++)
		{
			const COLLISION_PACKET& c = list[i];
			fprintf(f, "%d %s box %d/%d type %d flags %d pos %d %d %d yang %d size %d %d %d | model x %d..%d y %d..%d z %d..%d\n", t, ModelName(t), i, n, c.type, c.flags,
				c.xpos, c.ypos, c.zpos, c.yang, c.xsize, c.ysize, c.zsize, mb->min[0], mb->max[0], mb->min[1], mb->max[1], mb->min[2], mb->max[2]);
		}
	}

	fclose(f);
}

const char* const kCityNames[] = { "CHICAGO", "HAVANA", "VEGAS", "RIO", "MIAMI", "FRISCO", "LA", "NEWYORK", "NEWCASTLE", "TRAINING" };

void EditFileName(char* out, int size)
{
	snprintf(out, size, "EDITS/%s.EDIT.json", GameLevel >= 0 && GameLevel < 10 ? kCityNames[GameLevel] : "UNKNOWN");
}

// edits of this city as JSON: remove = raw packed words of deleted originals, add = type x y z yang, boxRemove = model + the 10 words of the packet
void SaveEdits()
{
	char name[64];
	EditFileName(name, sizeof(name));

#ifdef _WIN32
	_mkdir("EDITS");
#else
	mkdir("EDITS", 0777);
#endif

	FILE* f = fopen(name, "w");
	if (!f)
	{
		snprintf(gSaveMsg, sizeof(gSaveMsg), "SAVE FAILED: %s", name);
		return;
	}

	fprintf(f, "{\n\"version\": 1,\n\"city\": \"%s\",\n\"remove\": [\n", kCityNames[GameLevel]);
	for (int i = 0; i < gNumRemoved; i++)
		fprintf(f, " [%d,%d,%d,%d]%s\n", gRemoved[i].pos.vx, gRemoved[i].pos.vy, gRemoved[i].pos.vz, gRemoved[i].value, i + 1 < gNumRemoved ? "," : "");

	int adds = 0;
	fprintf(f, "],\n\"add\": [\n");
	for (int i = 0; i < gNumExtra; i++)
	{
		if (IsDeleted(&gExtra[i].po))
			continue;

		XZPAIR near;
		near.x = (gExtra[i].cx - (cells_across / 2)) * MAP_CELL_SIZE;
		near.z = (gExtra[i].cz - (cells_down / 2)) * MAP_CELL_SIZE;

		CELL_OBJECT co;
		QuickUnpackCellObject(&gExtra[i].po, &near, &co);
		fprintf(f, "%s [%d,%d,%d,%d,%d]", adds++ ? ",\n" : "", co.type, co.pos.vx, co.pos.vy, co.pos.vz, co.yang);
	}

	fprintf(f, "\n],\n\"boxRemove\": [\n");
	for (int i = 0; i < gNumBoxRemoved; i++)
	{
		const COLLISION_PACKET& c = gBoxRemoved[i].box;
		fprintf(f, " [%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d]%s\n", gBoxRemoved[i].model, c.type, c.xpos, c.ypos, c.zpos, c.flags, c.yang, c.empty, c.xsize, c.ysize, c.zsize,
			i + 1 < gNumBoxRemoved ? "," : "");
	}

	fprintf(f, "]\n}\n");
	fclose(f);
	snprintf(gSaveMsg, sizeof(gSaveMsg), "saved %d del %d add %d box", gNumRemoved, adds, gNumBoxRemoved);
}

// rows "[a,b,c]" of the list after "key": in the JSON text; returns the row count
int ParseRows(const char* text, const char* key, int perRow, int* out, int maxRows)
{
	const char* p = strstr(text, key);
	p = p ? strchr(p, '[') : NULL;
	if (!p)
		return 0;

	p++;
	int rows = 0;
	while (rows < maxRows)
	{
		while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '	')
			p++;

		if (*p != '[')
			break;		// ']' ends the list

		p++;
		for (int k = 0; k < perRow; k++)
		{
			char* end;
			out[rows * perRow + k] = (int)strtol(p, &end, 10);
			p = end;
			while (*p == ',' || *p == ' ')
				p++;
		}

		if (*p == ']')
			p++;

		rows++;
	}

	return rows;
}

// WASD pans over the ground along the screen axes, Q / E change the height; the free camera of DebugOverlay.cpp only renders from the position set here
void UpdateTopDown()
{
	g_FreeCameraControls = 0;
	g_FreeCameraVelocity.vx = g_FreeCameraVelocity.vy = g_FreeCameraVelocity.vz = 0;

	double ux = -inv_camera_matrix.m[1][0], uz = -inv_camera_matrix.m[1][2];
	const double len = sqrt(ux * ux + uz * uz);
	if (len > 1)
	{
		ux /= len;
		uz /= len;
	}
	else
	{
		ux = 0;
		uz = 1;
	}

	double rx = inv_camera_matrix.m[0][0], rz = inv_camera_matrix.m[0][2];
	const double rlen = sqrt(rx * rx + rz * rz);
	if (rlen > 1)
	{
		rx /= rlen;
		rz /= rlen;
	}
	const double speed = gViewH / 40.0 * (g_FreeCameraSpeed > 100 ? 4 : 1);
	const double fwd = ((gPanKeys & 1) ? 1 : 0) - ((gPanKeys & 2) ? 1 : 0);
	const double side = ((gPanKeys & 8) ? 1 : 0) - ((gPanKeys & 4) ? 1 : 0);

	gViewX += (int)((ux * fwd + rx * side) * speed);
	gViewZ += (int)((uz * fwd + rz * side) * speed);

	if (gPanKeys & 16)
		gViewH = gViewH * 15 / 16;
	if (gPanKeys & 32)
		gViewH = gViewH * 17 / 16 + 1;

	gViewH = gViewH < 1200 ? 1200 : (gViewH > 60000 ? 60000 : gViewH);

	g_FreeCameraPosition.vx = gViewX;
	g_FreeCameraPosition.vy = -gViewH;
	g_FreeCameraPosition.vz = gViewZ;
	g_FreeCameraRotation.vx = 1000;		// 1024 is straight down; a little less keeps the matrix well behaved
	g_FreeCameraRotation.vy = 0;
	g_FreeCameraRotation.vz = 0;
}

// the map regions stream in around the player, so the player's car follows the view when it gets far away (fast travel)
void FollowViewWithCar()
{
	static int cooldown = 0;
	if (player[0].playerCarId < 0)
		return;

	CAR_DATA* cp = &car_data[player[0].playerCarId];

	if (gGotoPlayer)
	{
		gGotoPlayer = false;
		gViewX = cp->hd.where.t[0];
		gViewZ = cp->hd.where.t[2];
		return;
	}

	if (cooldown > 0)
	{
		cooldown--;
		return;
	}

	const double dx = cp->hd.where.t[0] - gViewX, dz = cp->hd.where.t[2] - gViewZ;
	if (dx * dx + dz * dz < 2.0 * MAP_CELL_SIZE * 2.0 * MAP_CELL_SIZE)
		return;

	VECTOR ground = { gViewX, 0, gViewZ, 0 };
	LONGVECTOR4 pos = { gViewX, -MapHeight(&ground) - 120, gViewZ, 0 };
	InitCarPhysics(cp, &pos, cp->hd.direction);
	cooldown = 15;
}

void PanelRect(int x, int y, int w, int h, int r, int g, int b, int slot = 1)
{
	POLY_F4* p = (POLY_F4*)current->primptr;
	setPolyF4(p);
	setRGB0(p, r, g, b);
	p->x0 = x;		p->y0 = y;
	p->x1 = x + w;	p->y1 = y;
	p->x2 = x;		p->y2 = y + h;
	p->x3 = x + w;	p->y3 = y + h;
#if USE_PGXP && USE_EXTENDED_PRIM_POINTERS
	p->pgxp_index = 0xFFFF;
#endif
	setSemiTrans(p, 1);
	addPrim(current->ot + slot, p);		// ot[0] is drawn last: the text goes on top of the panel
	current->primptr += sizeof(POLY_F4);
}

struct PanelRow
{
	static int Put(const char* s, int tx, int y, int r, int g, int b)
	{
		SetTextColour(r, g, b);
		gFontScale = 0.72f;		// smaller text, the overlay lets the world show through
		PrintString((char*)s, tx, y);
		gFontScale = 1.0f;
		return y + 7;
	}
};

// narrow transparent panel on the left edge of the window (F1 adds the list of all keys beside it)
void DrawPanelBody()
{
	RECT16 vp;
	PsyX_GetPSXWidescreenMappedViewport(&vp);

	// the game never clears the screen (ground and sky cover it): a top-down view leaves holes where the last 3D frame showed through
	current->draw.isbg = gTopDown;
	current->draw.r0 = 36;
	current->draw.g0 = 42;
	current->draw.b0 = 38;

	const int x = vp.x + 3, w = 84;
	const int tx = x + 4;
	int y = 6;
	char text[96];

	const EditorObject* show = gHasSelected ? &gSelected : (gHasHover ? &gHover : NULL);
	const int rows = 16 + (show ? 4 : 0) + (gSaveMsg[0] ? 1 : 0) + (gEditorTest ? 1 : 0);

	PanelRect(x, 3, w, rows * 7 + 10, 40, 40, 48);
	PanelRect(x, 3, w, 10, 70, 100, 150);

	gPanelRight = x + w + (gHelp ? 153 : 0);
	gPanelBottom = rows * 7 + 13;
	gBackX = x + 3;
	gBackY = 15;
	gBackW = w - 6;
	gBackH = 9;
	PanelRect(gBackX, gBackY, gBackW, gBackH, 150, 60, 60);

	y = PanelRow::Put("MAP EDITOR", tx, y, 255, 255, 255);
	y = PanelRow::Put("< BACK (F4)", tx, y + 2, 255, 255, 255) + 3;
	y = PanelRow::Put(gTopDown ? "TAB: 3D view" : "TAB: 2D view", tx, y, 150, 200, 255);
	y = PanelRow::Put("F1 all keys", tx, y, 150, 200, 255) + 3;
	y = PanelRow::Put("LMB select", tx, y, 200, 200, 200);
	y = PanelRow::Put("drag = move", tx, y, 200, 200, 200);
	y = PanelRow::Put("WASD pan", tx, y, 200, 200, 200);
	y = PanelRow::Put("Q E zoom", tx, y, 200, 200, 200);
	y = PanelRow::Put("X del Z undo", tx, y, 200, 200, 200);
	y = PanelRow::Put("F5 save", tx, y, 200, 200, 200) + 4;

	if (show)
	{
		snprintf(text, sizeof(text), "%s%.12s", gHasSelected ? "" : "aim ", ModelName(show->co.type));
		y = PanelRow::Put(text, tx, y, 255, 220, 0);
		snprintf(text, sizeof(text), "%d %d", show->co.pos.vx, show->co.pos.vz);
		y = PanelRow::Put(text, tx, y, 255, 220, 0);

		int nBoxes;
		BoxList(show->co.type, nBoxes);
		snprintf(text, sizeof(text), "r%d y%d b%d/%d", show->co.yang, -show->co.pos.vy, nBoxes ? gBoxSel + 1 : 0, nBoxes);
		y = PanelRow::Put(text, tx, y, 255, 220, 0);
		y += 4;
	}

	snprintf(text, sizeof(text), "step %d", kSteps[gStepIndex]);
	y = PanelRow::Put(text, tx, y, 200, 200, 200);
	snprintf(text, sizeof(text), "snap %s", gSnap ? "on" : "off");
	y = PanelRow::Put(text, tx, y, 200, 200, 200);
	snprintf(text, sizeof(text), "undo %d", gUndoCount);
	y = PanelRow::Put(text, tx, y, 200, 200, 200);
	snprintf(text, sizeof(text), "file %d %d %d", gNumRemoved, gNumExtra, gNumBoxRemoved);
	y = PanelRow::Put(text, tx, y, 160, 160, 160);

	if (gSaveMsg[0])
		y = PanelRow::Put("saved", tx, y, 120, 255, 120);

	if (gEditorTest)
	{
		snprintf(text, sizeof(text), "bt %d>%d>%d", gTestCounts[0], gTestCounts[1], gTestCounts[2]);
		PanelRow::Put(text, tx, y, 160, 160, 160);
	}

	if (!gHelp)
		return;

	const int hx = x + w + 3, hw = 150;
	PanelRect(hx, 3, hw, 125, 40, 40, 48);
	PanelRect(hx, 3, hw, 10, 70, 100, 150);

	int hy = 6;
	const int htx = hx + 4;
	hy = PanelRow::Put("ALL KEYS", htx, hy, 255, 255, 255) + 4;
	hy = PanelRow::Put("arrows PgUp/Dn move", htx, hy, 200, 200, 200);
	hy = PanelRow::Put("G step  N snap", htx, hy, 200, 200, 200);
	hy = PanelRow::Put("R , .  rotate", htx, hy, 200, 200, 200);
	hy = PanelRow::Put("C copy  V paste", htx, hy, 200, 200, 200);
	hy = PanelRow::Put("[ ] model  B add", htx, hy, 200, 200, 200);
	hy = PanelRow::Put("K boxes  J pick", htx, hy, 200, 200, 200);
	hy = PanelRow::Put("H cut box", htx, hy, 200, 200, 200);
	hy = PanelRow::Put("SHIFT fast pan", htx, hy, 200, 200, 200);
	hy = PanelRow::Put("HOME to the car", htx, hy, 200, 200, 200);
	hy = PanelRow::Put("TAB 2D / 3D view", htx, hy, 200, 200, 200);
	hy = PanelRow::Put("3D: WASD QE fly", htx, hy, 200, 200, 200);
	hy = PanelRow::Put("F5 save  F4 exit", htx, hy, 200, 200, 200);
	hy = PanelRow::Put("file = removed,", htx, hy + 4, 160, 160, 160);
	hy = PanelRow::Put("added, cut boxes", htx, hy, 160, 160, 160);
}

void DrawPanel()
{
	DrawPanelBody();

	// semi transparent mode for the panel rectangles: added after them, so it is drawn first within the same ordering slot
	DR_TPAGE* tp = (DR_TPAGE*)current->primptr;
	setDrawTPage(tp, 1, 1, 0);
	addPrim(current->ot + 1, tp);
	current->primptr += sizeof(DR_TPAGE);
}

// ---- command port for the AI (plans/map-editor.md part 5): the editor reads EDITCMD.txt from the working directory every frame,
// runs its lines and writes the answers to EDITRES.txt. One command per line, numbers are world units, a model is its number or name.
FILE* gRes = NULL;

void Say(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(gRes, fmt, args);
	va_end(args);
	fputc('\n', gRes);
}

int ModelId(const char* token)
{
	if (isdigit((unsigned char)token[0]))
		return atoi(token);

	for (int i = 0; i < MAX_MODEL_SLOTS; i++)
	{
		if (strcasecmp(ModelName(i), token) == 0)
			return i;
	}

	return -1;
}

// calls fn for every prop whose centre is within r of x, z (cells around it, extras and deleted ones left out)
template <class F>
void ForObjectsAround(int x, int z, int r, F fn)
{
	const int cx = (x + units_across_halved) / MAP_CELL_SIZE, cz = (z + units_down_halved) / MAP_CELL_SIZE;
	const int cr = r / MAP_CELL_SIZE + 1;

	// a prop that straddles cells is listed in each of them
	static PACKED_CELL_OBJECT* seen[65536];
	int numSeen = 0;

	for (int j = cz - cr; j <= cz + cr; j++)
	{
		for (int i = cx - cr; i <= cx + cr; i++)
		{
			if (i < 0 || j < 0 || i >= cells_across || j >= cells_down)
				continue;

			CELL_ITERATOR ci;
			for (PACKED_CELL_OBJECT* po = GetFirstPackedCop(i, j, &ci, 0); po; po = GetNextPackedCop(&ci))
			{
				bool dup = false;
				for (int s = 0; s < numSeen && !dup; s++)
					dup = seen[s] == po;

				if (dup || numSeen >= 65536)
					continue;

				seen[numSeen++] = po;

				EditorObject o;
				o.packed = po;
				o.cellX = i;
				o.cellZ = j;
				QuickUnpackCellObject(po, &ci.nearCell, &o.co);

				const double dx = o.co.pos.vx - x, dz = o.co.pos.vz - z;
				if (dx * dx + dz * dz <= (double)r * r)
					fn(o);
			}
		}
	}
}

void DescribeObject(const EditorObject& o)
{
	int n;
	BoxList(o.co.type, n);
	Say("obj %d %s %d %d %d yang %d boxes %d%s", o.co.type, ModelName(o.co.type), o.co.pos.vx, o.co.pos.vy, o.co.pos.vz, o.co.yang, n, IsExtra(o.packed) ? " added" : "");
}

// world rectangle of a collision box as the game tests it (objcoll.c): centre, half extents, angle in 4096 units
void DescribeBox(const CELL_OBJECT& co, int idx, const COLLISION_PACKET& c)
{
	const MATRIX2* mat = &matrixtable[-co.yang & 63];
	const int midX = co.pos.vx + FIXEDH(c.xpos * mat->m[0][0] + c.zpos * mat->m[2][0]);
	const int midZ = co.pos.vz + FIXEDH(c.xpos * mat->m[0][2] + c.zpos * mat->m[2][2]);

	Say("box %d %s #%d at %d %d %d half %d %d high %d theta %d flags %d", co.type, ModelName(co.type), idx, midX, co.pos.vy + c.ypos, midZ,
		(c.xsize + 10) / 2, (c.zsize + 10) / 2, c.ysize, ((co.yang + c.yang) * 64) & 4095, c.flags);
}

bool SelectNearest(int x, int z, int type)
{
	double best = 1e18;
	EditorObject found;
	bool any = false;

	ForObjectsAround(x, z, MAP_CELL_SIZE, [&](const EditorObject& o)
	{
		const double dx = o.co.pos.vx - x, dz = o.co.pos.vz - z;
		if ((type < 0 || o.co.type == type) && dx * dx + dz * dz < best)
		{
			best = dx * dx + dz * dz;
			found = o;
			any = true;
		}
	});

	if (any)
	{
		gSelected = found;
		gHasSelected = true;
		gBoxSel = 0;
	}

	return any;
}

void RunCommand(const char* line)
{
	int a, b, c, e, f;
	char word[64], tok[64];

	if (sscanf(line, "%63s", word) != 1)
		return;

	Say("> %s", line);
	const char* args = line + strlen(word);

	if (!strcmp(word, "goto") && sscanf(args, "%d %d %d", &a, &b, &c) >= 2)
	{
		gTopDown = true;
		gViewX = a;
		gViewZ = b;
		gViewH = sscanf(args, "%d %d %d", &a, &b, &c) == 3 ? c : gViewH;
		Say("ok view %d %d height %d", gViewX, gViewZ, gViewH);
	}
	else if (!strcmp(word, "list") && sscanf(args, "%d %d %d", &a, &b, &c) == 3)
	{
		int count = 0;
		ForObjectsAround(a, b, c, [&](const EditorObject& o) { DescribeObject(o); count++; });
		Say("ok %d objects", count);
	}
	else if (!strcmp(word, "find") && sscanf(args, "%63s %d %d %d", tok, &a, &b, &c) == 4)
	{
		const int t = ModelId(tok);
		int count = 0;
		ForObjectsAround(a, b, c, [&](const EditorObject& o)
		{
			if (o.co.type == t)
			{
				DescribeObject(o);
				count++;
			}
		});
		Say("ok %d objects", count);
	}
	else if (!strcmp(word, "boxes") && sscanf(args, "%d %d %d", &a, &b, &c) == 3)
	{
		int count = 0;
		ForObjectsAround(a, b, c, [&](const EditorObject& o)
		{
			int n;
			COLLISION_PACKET* list = BoxList(o.co.type, n);
			for (int i = 0; i < n; i++)
			{
				DescribeBox(o.co, i, list[i]);
				count++;
			}
		});
		Say("ok %d boxes", count);
	}
	else if (!strcmp(word, "modelboxes") && sscanf(args, "%63s", tok) == 1)
	{
		const int t = ModelId(tok);
		int n;
		COLLISION_PACKET* list = BoxList(t, n);
		const ModelBox* mb = GetBox(t);
		for (int i = 0; i < n; i++)
			Say("modelbox %d %s #%d pos %d %d %d yang %d size %d %d %d flags %d model x %d..%d y %d..%d z %d..%d", t, ModelName(t), i, list[i].xpos, list[i].ypos, list[i].zpos,
				list[i].yang, list[i].xsize, list[i].ysize, list[i].zsize, list[i].flags, mb ? mb->min[0] : 0, mb ? mb->max[0] : 0, mb ? mb->min[1] : 0, mb ? mb->max[1] : 0,
				mb ? mb->min[2] : 0, mb ? mb->max[2] : 0);
		Say("ok %d boxes", n);
	}
	else if (!strcmp(word, "sel") && sscanf(args, "%d %d %63s", &a, &b, tok) >= 2)
	{
		const int type = sscanf(args, "%d %d %63s", &a, &b, tok) == 3 ? ModelId(tok) : -1;
		if (SelectNearest(a, b, type))
			DescribeObject(gSelected);
		else
			Say("fail nothing near");
	}
	else if (!strcmp(word, "move") && (c = 0, sscanf(args, "%d %d %d", &a, &b, &c)) >= 2)
	{
		if (!gHasSelected)
		{
			Say("fail no selection");
			return;
		}

		CELL_OBJECT n = gSelected.co;
		n.pos.vx += a;
		n.pos.vz += b;
		n.pos.vy -= c;
		ChangeSelected(n);
		DescribeObject(gSelected);
	}
	else if (!strcmp(word, "rot") && sscanf(args, "%d", &a) == 1)
	{
		RotateSelected(a);
		if (gHasSelected)
			DescribeObject(gSelected);
	}
	else if (!strcmp(word, "del"))
	{
		if (gHasSelected)
			DeleteSelected();
		Say("ok");
	}
	else if (!strcmp(word, "copy") && sscanf(args, "%d %d", &a, &b) == 2)
	{
		if (!gHasSelected)
		{
			Say("fail no selection");
			return;
		}

		CELL_OBJECT n = gSelected.co;
		n.pos.vx += a;
		n.pos.vz += b;
		const int i = AddExtra(n, false);
		if (i >= 0)
			SelectExtra(i);
		Say(i >= 0 ? "ok" : "fail");
	}
	else if (!strcmp(word, "add") && sscanf(args, "%63s %d %d %d %d", tok, &a, &b, &c, &e) == 5)
	{
		CELL_OBJECT n = {};
		n.type = ModelId(tok);
		n.pos.vx = a;
		n.pos.vy = b;
		n.pos.vz = c;
		n.yang = e & 63;
		const int i = n.type >= 0 ? AddExtra(n, false) : -1;
		if (i >= 0)
			SelectExtra(i);
		Say(i >= 0 ? "ok" : "fail");
	}
	else if (!strcmp(word, "cutbox") && sscanf(args, "%63s %d", tok, &a) == 2)
	{
		Say(RemoveBoxAt(ModelId(tok), a, false) ? "ok" : "fail");
	}
	else if (!strcmp(word, "cutmodel") && sscanf(args, "%63s", tok) == 1)
	{
		const int t = ModelId(tok);
		int n, cut = 0;
		BoxList(t, n);
		while (n > 0 && RemoveBoxAt(t, 0, false))
		{
			cut++;
			BoxList(t, n);
		}
		Say("ok cut %d", cut);
	}
	else if (!strcmp(word, "info"))
	{
		Say("cells %d x %d, cell size %d, world x %d..%d z %d..%d", cells_across, cells_down, MAP_CELL_SIZE, -units_across_halved, units_across_halved, -units_down_halved, units_down_halved);
		if (player[0].playerCarId >= 0)
			Say("player car at %d %d %d, view %d %d height %d", car_data[player[0].playerCarId].hd.where.t[0], car_data[player[0].playerCarId].hd.where.t[1], car_data[player[0].playerCarId].hd.where.t[2], gViewX, gViewZ, gViewH);
	}
	else if (!strcmp(word, "cam") && sscanf(args, "%d %d %d %d %d", &a, &b, &c, &e, &f) == 5)
	{
		// free 3D camera: position x y z (y up is negative), rotation x (256 = level, 1024 = down) and y (4096 = full turn)
		gTopDown = false;
		g_FreeCameraPosition.vx = a;
		g_FreeCameraPosition.vy = b;
		g_FreeCameraPosition.vz = c;
		g_FreeCameraRotation.vx = e;
		g_FreeCameraRotation.vy = f;
		g_FreeCameraRotation.vz = 0;
		Say("ok 3d camera");
	}
	else if (!strcmp(word, "straight") && sscanf(args, "%d", &a) == 1)
	{
		if (Driver2StraightsPtr && a >= 0)
			Say("straight %d at %d %d length %d angle %d", a, Driver2StraightsPtr[a].Midx, Driver2StraightsPtr[a].Midz, Driver2StraightsPtr[a].length, Driver2StraightsPtr[a].angle);
	}
	else if (!strcmp(word, "showboxes") && sscanf(args, "%d", &a) == 1)
	{
		gShowBoxes = a != 0;
		Say("ok");
	}
	else if (!strcmp(word, "shot"))
	{
		PsyX_TakeScreenshot();		// SCREENSHOT.BMP of the last presented frame, bottom-up
		Say("ok SCREENSHOT.BMP");
	}
	else if (!strcmp(word, "undo"))
	{
		Undo();
		Say("ok");
	}
	else if (!strcmp(word, "save"))
	{
		SaveEdits();
		Say("ok %s", gSaveMsg);
	}
	else
		Say("fail unknown or bad arguments");
}

void PollCommands()
{
	FILE* f = fopen("EDITCMD.txt", "rb");
	if (!f)
		return;

	char text[16384];
	text[fread(text, 1, sizeof(text) - 1, f)] = 0;
	fclose(f);
	remove("EDITCMD.txt");

	gRes = fopen("EDITRES.txt", "w");
	if (!gRes)
		return;

	for (char* line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n"))
		RunCommand(line);

	fprintf(gRes, "done\n");
	fclose(gRes);
	gRes = NULL;
}

void NoMouseLook(int x, int y, int dx, int dy)
{
}

void SetEditor(int on)
{
	gEditor = on;
	g_FreeCameraControls = 0;
	printf("Map editor: %s\n", on ? "ON" : "OFF");
}

} // namespace

void MapEditor_Draw()
{
	static int wasEditor = 0, warmup = 0;

	// the level camera needs a few frames before it holds a real position: start flying from there
	if (gEditor && !wasEditor && ++warmup > 30)
	{
		wasEditor = 1;
		g_FreeCameraPosition = camera_position;
		g_FreeCameraRotation = camera_angle;
		g_FreeCameraEnabled = 1;
		gViewX = camera_position.vx;
		gViewZ = camera_position.vz;
	}
	else if (!gEditor && wasEditor)
	{
		wasEditor = 0;
		warmup = 0;
		g_FreeCameraEnabled = 0;
	}

	if (!gEditor || !wasEditor)
		return;

	// top-down: the pointer stays free and picks; free look: relative mouse look through the free camera
	static bool sWasTopDown = false;
	if (gTopDown != sWasTopDown)
	{
		sWasTopDown = gTopDown;
		PsyX_SetCursorRelative(!gTopDown);
		SDL_ShowCursor(gTopDown ? 1 : 0);
	}
	g_dbg_gameDebugMouse = gTopDown ? NoMouseLook : FreeCameraMouseHandler;

	if (gTopDown)
	{
		UpdateTopDown();
		FollowViewWithCar();
	}

	PollCommands();

	gHasHover = PickObject(gHover);

	if (gEditorTest)
	{
		static int frame = 0;
		frame++;

		// boxes on from the start; frame 205 removes box 0 of the aimed prop's model, 215 undoes it
		gShowBoxes = true;
		if (frame == 1)
			WriteBoxReport();

		if (frame == 205)
		{
			// first model that has boxes: remove its box 0, 215 undoes it; the HUD shows the box counts
			int n = 0;
			gTestType = 0;
			while (gTestType < MAX_MODEL_SLOTS && (BoxList(gTestType, n), n == 0))
				gTestType++;

			static PACKED_CELL_OBJECT sDummy;		// the box test needs a selection even when nothing is under the pointer
			gSelected.packed = &sDummy;
			gSelected.co.type = gTestType;
			gHasSelected = true;
			gBoxSel = 0;
			gTestCounts[0] = n;
			RemoveSelectedBox();
			BoxList(gTestType, gTestCounts[1]);
		}
		else if (frame == 215)
		{
			Undo();
			BoxList(gTestType, gTestCounts[2]);
		}
		else if (frame == 222 && gHasHover)
		{
			// these two stay: the file written at 226 holds one removed original, the added copy and one removed box
			gSelected = gHover;
			gHasSelected = true;
			DeleteSelected();
		}
		else if (frame == 224)
		{
			static PACKED_CELL_OBJECT sDummy;		// the box test needs a selection even when nothing is under the pointer
			gSelected.packed = &sDummy;
			gSelected.co.type = gTestType;
			gHasSelected = true;
			gBoxSel = 0;
			RemoveSelectedBox();
		}
		else if (frame == 226)
			SaveEdits();

		// 60 copy the aimed prop 3000 units to the side, 80 move the copy, 100 turn it a quarter, 120 move the ORIGINAL,
		// 160 and 170 undo the original's move (delete + add in one go), then the delete at 190 and its undo at 200
		if (frame == 60 && gHasHover)
		{
			gSelected = gHover;
			gHasSelected = true;

			CELL_OBJECT n = gSelected.co;
			n.pos.vx += 3000;
			const int i = AddExtra(n, false);
			if (i >= 0)
				SelectExtra(i);
		}
		else if (frame == 80)
			MoveSelected(0, 1, 0);
		else if (frame == 100)
			RotateSelected(16);
		else if (frame == 120 && gHasHover)
		{
			gSelected = gHover;
			gHasSelected = true;
			MoveSelected(1, 0, 0);
		}
		else if (frame == 160 || frame == 170)
			Undo();
		else if (frame == 190 && gHasHover)
		{
			gSelected = gHover;
			gHasSelected = true;
			DeleteSelected();
		}
		else if (frame == 200)
			Undo();
	}

	// click selects; pressing on the selected prop and dragging moves it over the ground (applied on release, one undo step)
	const bool down = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;

	double mpx, mpy;
	MousePsx(mpx, mpy);
	const bool overPanel = mpx < gPanelRight && mpy < gPanelBottom;

	if (down && !gMouseWasDown && mpx >= gBackX && mpx < gBackX + gBackW && mpy >= gBackY && mpy < gBackY + gBackH)
	{
		gMouseWasDown = true;
		SetEditor(0);
		return;
	}

	if (down && !gMouseWasDown && overPanel)
		gMouseWasDown = true;		// a click on the panel selects nothing
	else if (down && !gMouseWasDown)
	{
		int x, z;
		if (gHasSelected && gHasHover && gHover.packed == gSelected.packed && AimOnPlane(gSelected.co.pos.vy, x, z))
		{
			gDragging = true;
			gGrabX = gSelected.co.pos.vx - x;
			gGrabZ = gSelected.co.pos.vz - z;
			gDragCo = gSelected.co;
		}
		else
		{
			gHasSelected = gHasHover;
			if (gHasHover)
				gSelected = gHover;
		}
	}

	if (gDragging && down && gHasSelected)
	{
		int x, z;
		if (AimOnPlane(gSelected.co.pos.vy, x, z))
		{
			const int step = kSteps[gStepIndex];
			x += gGrabX;
			z += gGrabZ;
			if (gSnap)
			{
				x = gSelected.co.pos.vx + (int)floor((x - gSelected.co.pos.vx) / (double)step + 0.5) * step;
				z = gSelected.co.pos.vz + (int)floor((z - gSelected.co.pos.vz) / (double)step + 0.5) * step;
			}

			gDragCo.pos.vx = x;
			gDragCo.pos.vz = z;
		}
	}
	else if (gDragging)
	{
		gDragging = false;
		if (gHasSelected && (gDragCo.pos.vx != gSelected.co.pos.vx || gDragCo.pos.vz != gSelected.co.pos.vz))
			ChangeSelected(gDragCo);
	}

	gMouseWasDown = down;

	CVECTOR hoverColor = { 120, 120, 120 };
	CVECTOR selectColor = { 255, 220, 0 };

	if (gHasHover)
		DrawBox(gHover, hoverColor);

	if (gHasSelected)
	{
		if (IsDeleted(gSelected.packed))
			gHasSelected = false;
		else
			DrawBox(gSelected, selectColor);
	}

	if (gDragging && gHasSelected)
	{
		EditorObject ghost = gSelected;
		ghost.co = gDragCo;
		CVECTOR dragColor = { 255, 140, 0 };
		DrawBox(ghost, dragColor);
	}

	DrawCollisionBoxes();

	DrawPanel();
}

// cell iterator hook (cell.c): objects the editor added to the cell that is being walked
// limit: linear scan at the end of every cell, upgrade: per-cell index once there are more than ~500 added objects
PACKED_CELL_OBJECT* EditorNextExtra(CELL_ITERATOR* pci)
{
	if (gAddsPending)
	{
		for (int i = 0; i < gNumExtra; i++)
		{
			if (gExtra[i].cx < 0)
			{
				gExtra[i].cx = (gPending[i].vx + units_across_halved) / MAP_CELL_SIZE;
				gExtra[i].cz = (gPending[i].vz + units_down_halved) / MAP_CELL_SIZE;
			}
		}

		gAddsPending = false;
	}

	while (pci->extraIdx < gNumExtra)
	{
		Extra& e = gExtra[pci->extraIdx++];
		if (e.cx == pci->cellX && e.cz == pci->cellZ && !IsDeleted(&e.po))
		{
			pci->ppco = &e.po;
			return &e.po;
		}
	}

	return NULL;
}

// level load (main.c LoadGameLevel): forget the last city's edits and read the file of this one
void EditorLoadEdits()
{
	gNumExtra = gNumRemoved = gNumBoxRemoved = gUndoCount = gDeletedCount = 0;
	gHasSelected = gHasHover = false;
	gAddsPending = false;
	gAppliedRemoved = gAppliedBoxes = 0;

	char name[64];
	EditFileName(name, sizeof(name));

	FILE* f = fopen(name, "rb");
	if (!f)
		return;

	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	char* text = (char*)malloc(size + 1);
	int* rows = (int*)malloc(sizeof(int) * 11 * kMaxRemoved);
	if (text && rows)
	{
		text[fread(text, 1, size, f)] = 0;

		int n = ParseRows(text, "\"remove\"", 4, rows, kMaxRemoved);
		for (int i = 0; i < n; i++)
		{
			gRemoved[i].pos.vx = rows[i * 4];
			gRemoved[i].pos.vy = rows[i * 4 + 1];
			gRemoved[i].pos.vz = rows[i * 4 + 2];
			gRemoved[i].value = rows[i * 4 + 3];
		}
		gNumRemoved = n;

		n = ParseRows(text, "\"add\"", 5, rows, kMaxExtra);
		for (int i = 0; i < n; i++)
		{
			CELL_OBJECT co = {};
			co.type = rows[i * 5];
			co.pos.vx = rows[i * 5 + 1];
			co.pos.vy = rows[i * 5 + 2];
			co.pos.vz = rows[i * 5 + 3];
			co.yang = rows[i * 5 + 4];

			// the cell size is not known yet at this point of the level load: EditorNextExtra finds the cells later
			gExtra[i].po = Pack(co);
			gExtra[i].cx = gExtra[i].cz = -1;
			gPending[i] = co.pos;
		}
		gNumExtra = n;
		gAddsPending = n > 0;

		n = ParseRows(text, "\"boxRemove\"", 11, rows, kMaxBoxRemoved);
		for (int i = 0; i < n; i++)
		{
			const int* r = rows + i * 11;
			COLLISION_PACKET& c = gBoxRemoved[i].box;

			gBoxRemoved[i].model = r[0];
			c.type = r[1]; c.xpos = r[2]; c.ypos = r[3]; c.zpos = r[4]; c.flags = r[5];
			c.yang = r[6]; c.empty = r[7]; c.xsize = r[8]; c.ysize = r[9]; c.zsize = r[10];
		}
		gNumBoxRemoved = n;

		printf("map edits %s: %d removed, %d added, %d boxes\n", name, gNumRemoved, gNumExtra, gNumBoxRemoved);
	}

	free(rows);
	free(text);
	fclose(f);
}

// region unpacked (spool.c RebaseCellData): put the deleted marker on every original the file removes
// limit: every object against every removed one, upgrade: sort gRemoved once when the file holds more than ~2000 removals
void EditorApplyRegion(PACKED_CELL_OBJECT* objects, int count)
{
	for (int r = 0; r < gNumRemoved; r++)
	{
		for (int i = 0; i < count; i++)
		{
			if (SameObject(objects[i], gRemoved[r]))
			{
				MarkDeleted(&objects[i]);
				gAppliedRemoved++;
			}
		}
	}
}

// model lump loaded (models.c ProcessMDSLump): take the removed collision boxes out of their models
void EditorApplyModels()
{
	for (int r = 0; r < gNumBoxRemoved; r++)
	{
		int n;
		COLLISION_PACKET* list = BoxList(gBoxRemoved[r].model, n);

		for (int i = 0; i < n; i++)
		{
			if (memcmp(&list[i], &gBoxRemoved[r].box, sizeof(COLLISION_PACKET)) == 0)
			{
				memmove(&list[i], &list[i + 1], (n - i - 1) * sizeof(COLLISION_PACKET));
				*GET_MODEL_DATA(int, modelpointers[gBoxRemoved[r].model], collision_block) = n - 1;
				gAppliedBoxes++;
				break;
			}
		}
	}
}

// debug menu (pause.c): switch the editor on or off
void MapEditor_Toggle()
{
	SetEditor(!gEditor);
}

// main.c: 1 while the top-down camera is on (the view cone of the map walk is opened up)
int EditorTopDownView()
{
	return gEditor && gTopDown && g_FreeCameraEnabled;
}

// returns 1 when the key was used by the editor
int MapEditor_Key(int nKey, char down)
{
	if (down && nKey == SDL_SCANCODE_F4)
	{
		SetEditor(!gEditor);
		return 1;
	}

	if (!gEditor)
		return 0;

	if (down && nKey == SDL_SCANCODE_F5)
	{
		SaveEdits();
		return 1;
	}

	// free camera movement bits (DebugOverlay.cpp DoFreeCamera): 1 forward, 2 back, 4 left, 8 right, 16 up, 32 down
	int bit = 0;
	switch (nKey)
	{
		case SDL_SCANCODE_W: bit = 1; break;
		case SDL_SCANCODE_S: bit = 2; break;
		case SDL_SCANCODE_A: bit = 4; break;
		case SDL_SCANCODE_D: bit = 8; break;
		case SDL_SCANCODE_E: bit = 16; break;
		case SDL_SCANCODE_Q: bit = 32; break;
	}

	if (bit)
	{
		// top-down: W S A D = up down left right on the screen, E / Q = higher / lower
		int& keys = gTopDown ? gPanKeys : g_FreeCameraControls;
		if (gTopDown)
			bit = (nKey == SDL_SCANCODE_W) ? 1 : (nKey == SDL_SCANCODE_S) ? 2 : (nKey == SDL_SCANCODE_A) ? 4 : (nKey == SDL_SCANCODE_D) ? 8 : (nKey == SDL_SCANCODE_Q) ? 16 : 32;

		if (down)
			keys |= bit;
		else
			keys &= ~bit;

		return 1;
	}

	if (down && nKey == SDL_SCANCODE_HOME)
	{
		gGotoPlayer = true;
		return 1;
	}

	if (down && nKey == SDL_SCANCODE_F1)
	{
		gHelp = !gHelp;
		return 1;
	}

	if (down && nKey == SDL_SCANCODE_TAB)
	{
		gTopDown = !gTopDown;
		gPanKeys = g_FreeCameraControls = 0;
		if (!gTopDown)
			g_FreeCameraRotation.vx = 256;		// look ahead again
		return 1;
	}

	if (nKey == SDL_SCANCODE_LSHIFT)
	{
		g_FreeCameraSpeed = down ? 160 : 32;
		return 1;
	}

	if (!down)
		return 0;

	if (nKey == SDL_SCANCODE_X || nKey == SDL_SCANCODE_DELETE)
		DeleteSelected();
	else if (nKey == SDL_SCANCODE_Z)
		Undo();
	else if (nKey == SDL_SCANCODE_RIGHT)
		MoveSelected(1, 0, 0);
	else if (nKey == SDL_SCANCODE_LEFT)
		MoveSelected(-1, 0, 0);
	else if (nKey == SDL_SCANCODE_UP)
		MoveSelected(0, 1, 0);
	else if (nKey == SDL_SCANCODE_DOWN)
		MoveSelected(0, -1, 0);
	else if (nKey == SDL_SCANCODE_PAGEUP)
		MoveSelected(0, 0, 1);
	else if (nKey == SDL_SCANCODE_PAGEDOWN)
		MoveSelected(0, 0, -1);
	else if (nKey == SDL_SCANCODE_G)
		gStepIndex = (gStepIndex + 1) % (int)(sizeof(kSteps) / sizeof(kSteps[0]));
	else if (nKey == SDL_SCANCODE_N)
		gSnap = !gSnap;
	else if (nKey == SDL_SCANCODE_R)
		RotateSelected(16);
	else if (nKey == SDL_SCANCODE_PERIOD)
		RotateSelected(1);
	else if (nKey == SDL_SCANCODE_COMMA)
		RotateSelected(-1);
	else if (nKey == SDL_SCANCODE_C && gHasSelected)
	{
		gClip = gSelected.co;
		gHasClip = true;
	}
	else if (nKey == SDL_SCANCODE_V && gHasClip)
		PlaceAtAim(gClip);
	else if (nKey == SDL_SCANCODE_LEFTBRACKET)
		CycleModel(-1);
	else if (nKey == SDL_SCANCODE_RIGHTBRACKET)
		CycleModel(1);
	else if (nKey == SDL_SCANCODE_B)
		AddModelAtAim();
	else if (nKey == SDL_SCANCODE_K)
		gShowBoxes = !gShowBoxes;
	else if (nKey == SDL_SCANCODE_J && gHasSelected)
	{
		int n;
		BoxList(gSelected.co.type, n);
		gBoxSel = n ? (gBoxSel + 1) % n : 0;
	}
	else if (nKey == SDL_SCANCODE_H)
		RemoveSelectedBox();
	else
		return 0;

	return 1;
}
