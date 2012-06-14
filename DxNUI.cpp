/*
 *  DxNUI.cpp
 *
 *  Created: 2012/06/13
 *  Author: yoneken
 */

#include<Windows.h>
#include <d3d9.h>
#include <ole2.h>
#include <NuiApi.h>
#pragma comment(lib, "Kinect10.lib")

#define STDERR(str) OutputDebugString(L##str)

typedef struct D3DXVECTOR3 {
	FLOAT x;
	FLOAT y;
	FLOAT z;
} D3DXVECTOR3;

// body structure of MikuMikuDance(MMD)
enum {
	MMD_SKELETON_POSITION_CENTER = 0,
	MMD_SKELETON_POSITION_NECK,
	MMD_SKELETON_POSITION_HEAD,
	MMD_SKELETON_POSITION_SHOULDER_LEFT,
	MMD_SKELETON_POSITION_ELBOW_LEFT,
	MMD_SKELETON_POSITION_WRIST_LEFT,
	MMD_SKELETON_POSITION_SHOULDER_RIGHT,
	MMD_SKELETON_POSITION_ELBOW_RIGHT,
	MMD_SKELETON_POSITION_WRIST_RIGHT,
	MMD_SKELETON_POSITION_LEG_LEFT,
	MMD_SKELETON_POSITION_KNEE_LEFT,
	MMD_SKELETON_POSITION_ANKLE_LEFT,
	MMD_SKELETON_POSITION_LEG_RIGHT,
	MMD_SKELETON_POSITION_KNEE_RIGHT,
	MMD_SKELETON_POSITION_ANKLE_RIGHT,
	MMD_SKELETON_POSITION_TORSO,
	MMD_SKELETON_POSITION_HAND_LEFT,
	MMD_SKELETON_POSITION_HAND_RIGHT,
	MMD_SKELETON_POSITION_COUNT
};

float Colors[][3] ={{1,1,1},{0,1,1},{0,0,1},{0,1,0},{1,1,0},{1,0,0},{1,.5,0}};	// Order is Blue-Green-Red


// export functions
__declspec(dllexport) bool __stdcall OpenNIInit(HWND, bool, LPDIRECT3DDEVICE9, WCHAR*, CHAR*);
__declspec(dllexport) void __stdcall OpenNIClean(void);
__declspec(dllexport) void __stdcall OpenNIDrawDepthMap(bool);
__declspec(dllexport) void __stdcall OpenNIDepthTexture(IDirect3DTexture9**);
__declspec(dllexport) void __stdcall OpenNIGetSkeltonJointPosition(int, D3DXVECTOR3*);
__declspec(dllexport) void __stdcall OpenNIIsTracking(bool*);
__declspec(dllexport) void __stdcall OpenNIGetVersion(float*);

INuiSensor *pNuiSensor;
HANDLE hNextDepthFrameEvent;
HANDLE hNextSkeletonEvent;
HANDLE pDepthStreamHandle;

D3DXVECTOR3 BP_Vector[MMD_SKELETON_POSITION_COUNT];
Vector4 BP_Zero;
IDirect3DTexture9 *DepthTex = NULL;
Vector4 skels[NUI_SKELETON_COUNT][NUI_SKELETON_POSITION_COUNT];
bool tracked = false;
int trackedDataIndex = 0;

inline float convNUI2NI(float val){ return val*1000.0f; }	// NUI default is meter and NI default is millimeter.

void storeNuiDepth(bool waitflag)
{
	NUI_IMAGE_FRAME depthFrame;

	if(waitflag) WaitForSingleObject(hNextDepthFrameEvent, INFINITE);
	else if(WAIT_OBJECT_0 != WaitForSingleObject(hNextDepthFrameEvent, 0)) return;

	HRESULT hr = pNuiSensor->NuiImageStreamGetNextFrame(
		pDepthStreamHandle,
		0,
		&depthFrame );
	if( FAILED( hr ) ){
		return;
	}
	if(depthFrame.eImageType != NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX)
		STDERR("Depth type is not match with the depth and players\r\n");

	INuiFrameTexture *pTexture = depthFrame.pFrameTexture;

	NUI_LOCKED_RECT LockedRect;
	pTexture->LockRect( 0, &LockedRect, NULL, 0 );
	D3DLOCKED_RECT LPdest;
	DepthTex->LockRect(0,&LPdest,NULL, 0);

	if( LockedRect.Pitch != 0 ){
		unsigned short *pBuffer = (unsigned short *)LockedRect.pBits;
		unsigned char *pDestImage=(unsigned char*)LPdest.pBits;

		NUI_SURFACE_DESC pDesc;
		pTexture->GetLevelDesc(0, &pDesc);

		unsigned short *p = (unsigned short *)pBuffer;
		for(int y=0;y<60;y++){
			for(int x=0;x<80;x++){
				unsigned char depth = (unsigned char)((*pBuffer & 0xff00)>>8);
				unsigned short playerID = NuiDepthPixelToPlayerIndex(*pBuffer);
				*pDestImage = (unsigned char)(Colors[playerID][0] * depth);
				pDestImage++;
				*pDestImage = (unsigned char)(Colors[playerID][1] * depth);
				pDestImage++;
				*pDestImage = (unsigned char)(Colors[playerID][2] * depth);
				pDestImage++;
				*pDestImage = 255;
				pDestImage++;
				pBuffer++;
			}
			pDestImage += (128-80)*4;
		}
		DepthTex->UnlockRect(0);
		pTexture->UnlockRect(0);
	}
	else{
		STDERR("Buffer length of received texture is bogus\r\n");
	}
	pNuiSensor->NuiImageStreamReleaseFrame( pDepthStreamHandle, &depthFrame );
}

void storeNuiSkeleton(bool waitflag)
{
	if(waitflag) WaitForSingleObject(hNextSkeletonEvent, INFINITE);
	else if(WAIT_OBJECT_0 != WaitForSingleObject(hNextSkeletonEvent, 0)) return;

	NUI_SKELETON_FRAME SkeletonFrame = {0};
	HRESULT hr = pNuiSensor->NuiSkeletonGetNextFrame( 0, &SkeletonFrame );

	bool bFoundSkeleton = false;
	for( int i = 0 ; i < NUI_SKELETON_COUNT ; i++ ){
		if( SkeletonFrame.SkeletonData[i].eTrackingState == NUI_SKELETON_TRACKED ){
			bFoundSkeleton = true;
			trackedDataIndex = i;
			tracked = true;
		}
	}

	// no skeletons!
	if( !bFoundSkeleton ){
		tracked = false;
		return;
	}

	// smooth out the skeleton data
	pNuiSensor->NuiTransformSmooth(&SkeletonFrame,NULL);

	// store each skeleton color according to the slot within they are found.
	for( int i = 0 ; i < NUI_SKELETON_COUNT ; i++ )
	{
		if( (SkeletonFrame.SkeletonData[i].eTrackingState == NUI_SKELETON_TRACKED)){
			memcpy(skels[i], SkeletonFrame.SkeletonData[i].SkeletonPositions, sizeof(Vector4)*NUI_SKELETON_POSITION_COUNT);
		}
	}
}

void setBP(Vector4 ejoint, D3DXVECTOR3* point)
{
	point->x = convNUI2NI(ejoint.x - BP_Zero.x);
	point->y = convNUI2NI(ejoint.y - BP_Zero.y);
	point->z = convNUI2NI(ejoint.z - BP_Zero.z);
}

// DllMain
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	switch( fdwReason )
	{
	case DLL_PROCESS_ATTACH:
		break;

	case DLL_PROCESS_DETACH:
		OpenNIClean();
		break;

	case DLL_THREAD_ATTACH:
		break;

	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}

__declspec(dllexport) bool __stdcall OpenNIInit(HWND hWnd, bool EngFlag, LPDIRECT3DDEVICE9 lpDevice, WCHAR* f_path, CHAR* onifilename)
{
	HRESULT hr;

	hr = NuiCreateSensorByIndex(0, &pNuiSensor);
	if(FAILED(hr)){
		if(EngFlag) MessageBox(hWnd, L"Kinect is not connected", L"Kinect", MB_OK);
		else MessageBox(hWnd, L"Kinectが接続されていません", L"Kinect", MB_OK);
		return false;
	}

	hr = pNuiSensor->NuiInitialize(
		NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX | 
		NUI_INITIALIZE_FLAG_USES_SKELETON
		);
	if(FAILED(hr)){
		if(EngFlag) MessageBox(hWnd, L"Could not initialize the kinect", L"Kinect", MB_OK);
		else MessageBox(hWnd, L"Kinectが初期化できません", L"Kinect", MB_OK);
		return false;
	}

	hNextDepthFrameEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
	hNextSkeletonEvent = CreateEvent( NULL, TRUE, FALSE, NULL );

	if(HasSkeletalEngine(pNuiSensor)){
		hr = pNuiSensor->NuiSkeletonTrackingEnable( hNextSkeletonEvent, 
			//NUI_SKELETON_TRACKING_FLAG_TITLE_SETS_TRACKED_SKELETONS |
			//NUI_SKELETON_TRACKING_FLAG_ENABLE_SEATED_SUPPORT 
			0
			);
		if(FAILED(hr)){
			if(EngFlag) MessageBox(hWnd, L"This kinect cannot track skeleton structure", L"Kinect", MB_OK);
			else MessageBox(hWnd, L"Kinectがボーン構造をサポートしていません", L"Kinect", MB_OK);
			return false;
		}
	}

	hr = pNuiSensor->NuiImageStreamOpen(
		HasSkeletalEngine(pNuiSensor) ? NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX : NUI_IMAGE_TYPE_DEPTH,
		NUI_IMAGE_RESOLUTION_80x60,
		0,
		2,
		hNextDepthFrameEvent,
		&pDepthStreamHandle );
	if(FAILED(hr)){
		if(EngFlag) MessageBox(hWnd, L"Could not connect to depth sensor", L"Kinect", MB_OK);
		else MessageBox(hWnd, L"Kinectの深度センサーを認識できません", L"Kinect", MB_OK);
		return false;
	}

	if(FAILED(lpDevice->CreateTexture(128, 64, 1, 0, D3DFMT_A8R8G8B8,D3DPOOL_MANAGED, &DepthTex, NULL))){
		if(EngFlag) MessageBox(hWnd, L"Could not make depth texture", L"Kinect", MB_OK);
		else MessageBox(hWnd, L"DepthTex作成失敗", L"Kinect", MB_OK);
		return false;
	}

	return true;
}

__declspec(dllexport) void __stdcall OpenNIClean(void)
{
	if(DepthTex){
		DepthTex->Release();
		DepthTex=NULL;
	}

	CloseHandle( hNextDepthFrameEvent );
	hNextDepthFrameEvent = NULL;

	CloseHandle( hNextSkeletonEvent );
	hNextSkeletonEvent = NULL;

	if(pNuiSensor){
		if(HasSkeletalEngine(pNuiSensor)) pNuiSensor->NuiSkeletonTrackingDisable();
		pNuiSensor->NuiShutdown();
		pNuiSensor->Release();
		pNuiSensor = NULL;
	}
}

__declspec(dllexport) void __stdcall OpenNIDrawDepthMap(bool waitflag)
{
	storeNuiDepth(waitflag);
	storeNuiSkeleton(waitflag);

	static int count = 0;

	if(tracked == true){
		if(count < 10){
			count++;
		}else if(count == 10){
			BP_Zero.x = skels[trackedDataIndex][NUI_SKELETON_POSITION_SPINE].x;
			BP_Zero.z = skels[trackedDataIndex][NUI_SKELETON_POSITION_SPINE].z;
			BP_Zero.y = (skels[trackedDataIndex][NUI_SKELETON_POSITION_HIP_LEFT].y + skels[trackedDataIndex][NUI_SKELETON_POSITION_HIP_RIGHT].y)/2.0f;
			count++;
		}else{
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_SPINE], &BP_Vector[MMD_SKELETON_POSITION_CENTER]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_SHOULDER_CENTER], &BP_Vector[MMD_SKELETON_POSITION_NECK]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_HEAD], &BP_Vector[MMD_SKELETON_POSITION_HEAD]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_SHOULDER_LEFT], &BP_Vector[MMD_SKELETON_POSITION_SHOULDER_LEFT]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_ELBOW_LEFT], &BP_Vector[MMD_SKELETON_POSITION_ELBOW_LEFT]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_WRIST_LEFT], &BP_Vector[MMD_SKELETON_POSITION_WRIST_LEFT]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_SHOULDER_RIGHT], &BP_Vector[MMD_SKELETON_POSITION_SHOULDER_RIGHT]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_ELBOW_RIGHT], &BP_Vector[MMD_SKELETON_POSITION_ELBOW_RIGHT]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_WRIST_RIGHT], &BP_Vector[MMD_SKELETON_POSITION_WRIST_RIGHT]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_HIP_LEFT], &BP_Vector[MMD_SKELETON_POSITION_LEG_LEFT]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_KNEE_LEFT], &BP_Vector[MMD_SKELETON_POSITION_KNEE_LEFT]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_ANKLE_LEFT], &BP_Vector[MMD_SKELETON_POSITION_ANKLE_LEFT]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_HIP_RIGHT], &BP_Vector[MMD_SKELETON_POSITION_LEG_RIGHT]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_KNEE_RIGHT], &BP_Vector[MMD_SKELETON_POSITION_KNEE_RIGHT]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_ANKLE_RIGHT], &BP_Vector[MMD_SKELETON_POSITION_ANKLE_RIGHT]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_SPINE], &BP_Vector[MMD_SKELETON_POSITION_TORSO]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_HAND_LEFT], &BP_Vector[MMD_SKELETON_POSITION_HAND_LEFT]);
			setBP(skels[trackedDataIndex][NUI_SKELETON_POSITION_HAND_RIGHT], &BP_Vector[MMD_SKELETON_POSITION_HAND_RIGHT]);
			BP_Vector[MMD_SKELETON_POSITION_WRIST_LEFT] = BP_Vector[MMD_SKELETON_POSITION_HAND_LEFT];
			BP_Vector[MMD_SKELETON_POSITION_WRIST_RIGHT] = BP_Vector[MMD_SKELETON_POSITION_HAND_RIGHT];
			BP_Vector[MMD_SKELETON_POSITION_CENTER].y = (BP_Vector[MMD_SKELETON_POSITION_LEG_LEFT].y + BP_Vector[MMD_SKELETON_POSITION_LEG_RIGHT].y)/2.0f;
		}
	}else{
		count = 0;
	}
}

__declspec(dllexport) void __stdcall OpenNIDepthTexture(IDirect3DTexture9** lpTex)
{
	*lpTex = DepthTex;
}

__declspec(dllexport) void __stdcall OpenNIGetSkeltonJointPosition(int num,D3DXVECTOR3* vec)
{
	*vec = BP_Vector[num];
}

__declspec(dllexport) void __stdcall OpenNIIsTracking(bool* lpb)
{
	*lpb = tracked;
}

__declspec(dllexport) void __stdcall OpenNIGetVersion(float* ver)
{
	*ver = 1.5f;
}