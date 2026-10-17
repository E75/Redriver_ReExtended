#ifndef CAMERA_H
#define CAMERA_H

#define CAMERA_COLLIDER_CARID (MAX_CARS+1)

#ifdef PSX
#define VIEW_DRAW_DISTANCE 16000
#else
#define VIEW_DRAW_DISTANCE 22000
#endif

extern VECTOR camera_position;
extern char old_camera_change;
extern char camera_change;

extern MATRIX face_camera_work;
extern MATRIX camera_matrix;

extern char cameraview;
extern char tracking_car;
extern int CameraCnt;

extern int TargetCar;
extern int CameraCar;

extern SVECTOR camera_angle;
extern int gCameraAngle;
extern int gCameraOrbit;			// right stick orbit camera on/off (Options - Gameplay - Secret)
extern int gCameraZoom;				// follow camera distance index: 1.0 / 1.2 / 1.5 / 1.8 / 2.0 (Extended Settings)
#define CAMERA_DISTANCE_MENU	0		// 1 shows the Camera Distance row in Extended Settings and applies the saved choice

extern short gCameraDistance;
extern short gCameraMaxDistance;
extern short gCameraDefaultScrZ;

extern VECTOR gCameraOffset;

extern int switch_detail_distance;

extern void InitCamera(PLAYER *lp); // 0x0001F5F4
extern void ModifyCamera(); // 0x0001FA20

extern int CameraCollisionCheck(); // 0x0001FC18

extern void PlaceCameraFollowCar(PLAYER *lp); // 0x0002003C
extern void PlaceCameraAtLocation(PLAYER *lp, int zoom); // 0x00020904
extern void PlaceCameraInCar(PLAYER *lp, int BumperCam); // 0x0002050C


extern int PointAtTarget(VECTOR* pPosition, VECTOR* pTarget, SVECTOR* pAngleVec);

extern int OK_To_Zoom(); // 0x00020BC0

extern void SetBasePos(VECTOR *pVec); // 0x00020C70


#endif
