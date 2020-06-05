// This sample was made by Morten S. Mikkelsen
// It illustrates how to do compositing of bump maps in complex scenarios using the surface gradient based framework.


#define SHOW_DEMO_SCENE

#include "DXUT.h"
#include "DXUTcamera.h"
#include "DXUTgui.h"
//#include "DXUTsettingsDlg.h"
#include "SDKmisc.h"
//#include "SDKMesh.h"
#include <d3d11_2.h>
#include "strsafe.h"
#include <stdlib.h>
#include <math.h>

#include "scenegraph.h"
#ifdef SHOW_DEMO_SCENE
#include "shadows.h"
#include "canvas.h"
#endif
#include "debug_volume_base.h"


#define RECOMPILE_SCRBOUND_CS_SHADER

#define DISABLE_QUAT

#include <geommath/geommath.h>
#include "shader.h"
#include "shaderpipeline.h"
#include "std_cbuffer.h"
#include "shaderutils.h"
#include "texture_rt.h"
#include "buffer.h"

#include <wchar.h>

CDXUTDialogResourceManager g_DialogResourceManager;
CDXUTTextHelper *		g_pTxtHelper = NULL;

CFirstPersonCamera                  g_Camera;


CShader scrbound_shader;
CShader volumelist_coarse_shader;
CShader volumelist_exact_shader;

static CShader debug_vol_vert_shader, debug_vol_pix_shader;
static CShaderPipeline debug_volume_shader_pipeline;

#ifndef SHOW_DEMO_SCENE
static CShader vert_shader, pix_shader;
static CShader vert_shader_basic;

static ID3D11InputLayout * g_pVertexLayout = NULL;
static ID3D11InputLayout * g_pVertexSimpleLayout = NULL;

CShaderPipeline shader_dpthfill_pipeline;
CShaderPipeline shader_pipeline;

ID3D11Buffer * g_pMeshInstanceCB = NULL;

#define MODEL_NAME		"ground2_reduced.fil"
#define MODEL_PATH		".\\"



#define MODEL_PATH_W	WIDEN(MODEL_PATH)


#include "mesh_fil.h"
#endif

ID3D11Buffer * g_pGlobalsCB = NULL;

CBufferObject g_VolumeDataBuffer;
CBufferObject g_ScrSpaceAABounds;

ID3D11Buffer * g_pVolumeClipInfo = NULL;

CBufferObject g_volumeListBuffer;
CBufferObject g_OrientedBounds;

#define WIDEN2(x)		L ## x
#define WIDEN(x)		WIDEN2(x)





#ifndef SHOW_DEMO_SCENE

#define MAX_LEN			64
#define NR_TEXTURES		1


const WCHAR tex_names[NR_TEXTURES][MAX_LEN] = {L"normals.png"};
const char stex_names[NR_TEXTURES][MAX_LEN] = {"g_norm_tex"};

static ID3D11ShaderResourceView * g_pTexturesHandler[NR_TEXTURES];
#else
#include "meshimport/meshdraw.h"
#endif

#ifndef M_PI
	#define M_PI 3.1415926535897932384626433832795
#endif

bool CALLBACK ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings, void* pUserContext );
void CALLBACK OnFrameMove( double fTime, float fElapsedTime, void* pUserContext );
bool CALLBACK IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output, const CD3D11EnumDeviceInfo *DeviceInfo, DXGI_FORMAT BackBufferFormat, bool bWindowed, void* pUserContext );
HRESULT CALLBACK OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext );
void CALLBACK OnD3D11DestroyDevice( void* pUserContext );
void CALLBACK OnKeyboard( UINT nChar, bool bKeyDown, bool bAltDown, void* pUserContext );
HRESULT CALLBACK OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext );
void CALLBACK OnD3D11ReleasingSwapChain( void* pUserContext );
LRESULT CALLBACK MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool* pbNoFurtherProcessing, void* pUserContext );

void myFrustum(float * pMat, const float fLeft, const float fRight, const float fBottom, const float fTop, const float fNear, const float fFar);


#ifndef SHOW_DEMO_SCENE
CMeshFil g_cMesh;
#endif


static int g_iCullMethod = 1;
#ifdef SHOW_DEMO_SCENE
static int g_iVisualMode = 1;
#else
static int g_iVisualMode = 0;
#endif
static int g_iMenuVisib = 1;

static int g_iDecalBlendingMethod = 0;		// additive, masked, decal dir
static bool g_bUseSecondaryUVsetOnPirate = true;
static int g_iBumpFromHeightMapMethod = 0;
static bool g_bEnableDecalMipMapping = true;
static bool g_bEnableDecals = true;
static bool g_bShowNormalsWS = false;
static bool g_bIndirectSpecular = true;
static bool g_bShowDebugVolumes = false;
static bool g_bEnableShadows = true;


#include "volume_definitions.h"
#include "VolumeTiling.h"



int g_iSqrtNrVolumes = 0;
int g_iNrVolumes = MAX_NR_VOLUMES_PER_CAMERA;

SFiniteVolumeData g_sLgtData[MAX_NR_VOLUMES_PER_CAMERA];
SFiniteVolumeBound g_sLgtColiData[MAX_NR_VOLUMES_PER_CAMERA];

CVolumeTiler g_cVolumeTiler;

static float frnd() { return (float) (((double) (rand() % (RAND_MAX+1))) / RAND_MAX); }

CTextureObject g_tex_depth;
#ifdef SHOW_DEMO_SCENE
CShadowMap g_shadowMap;
CCanvas g_canvas;
#endif

void InitApp()
{
    
	g_Camera.SetRotateButtons( true, false, false );
    g_Camera.SetEnablePositionMovement( true );

#ifdef SHOW_DEMO_SCENE
	const float scale = 0.04f;
#else
	const float scale = 1.0f;	// 0.1f
#endif
    g_Camera.SetScalers( 0.2f*0.005f, 3*100.0f * scale );
    DirectX::XMVECTOR vEyePt, vEyeTo;

	Vec3 cam_pos = 75.68f*Normalize(Vec3(16,0,40));	// normal

	vEyePt = DirectX::XMVectorSet(cam_pos.x, cam_pos.y, -cam_pos.z, 1.0f);
	//vEyeTo = DirectX::XMVectorSet(0.0f, 2.0f, 0.0f, 1.0f);
	vEyeTo = DirectX::XMVectorSet(10.0f, 2.0f, 0.0f, 1.0f);


	// surfgrad demo
#ifdef SHOW_DEMO_SCENE
	const float g_O = 2*2.59f;
	const float g_S = -1.0f;		// convert unity scene to right hand frame.

	vEyeTo = DirectX::XMVectorSet(g_S*3.39f+g_O, 1.28f+0.3, -0.003f+1.5, 1.0f);
	vEyePt = DirectX::XMVectorSet(g_S*3.39f+g_O-10, 1.28f+2.5, -0.003f, 1.0f);
#endif

	
    g_Camera.SetViewParams( vEyePt, vEyeTo );

	g_iSqrtNrVolumes = (int) sqrt((double) g_iNrVolumes);
	assert((g_iSqrtNrVolumes*g_iSqrtNrVolumes)<=g_iNrVolumes);
	g_iNrVolumes = g_iSqrtNrVolumes*g_iSqrtNrVolumes;
}

void RenderText()
{
	g_pTxtHelper->Begin();
	g_pTxtHelper->SetInsertionPos( 2, 0 );
	g_pTxtHelper->SetForegroundColor(DirectX::XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f));
	g_pTxtHelper->DrawTextLine( DXUTGetFrameStats(true) );

	if(g_iMenuVisib!=0)
	{
#ifndef SHOW_DEMO_SCENE
		g_pTxtHelper->DrawTextLine(L"This scene is forward lit by 1024 lights of different shapes. High performance is achieved using FPTL.\n");
#else
		g_pTxtHelper->DrawTextLine(L"This scene illustrates advanced bump map compositing using the surface gradient based framework.\n");
#endif
		g_pTxtHelper->DrawTextLine(L"Rotate the camera by using the mouse while pressing and holding the left mouse button.\n");
		g_pTxtHelper->DrawTextLine(L"Move the camera by using the arrow keys or: w, a, s, d\n");
		g_pTxtHelper->DrawTextLine(L"Hide menu using the x key.\n");


#ifdef SHOW_DEMO_SCENE
		// U
		if(g_bUseSecondaryUVsetOnPirate)
			g_pTxtHelper->DrawTextLine(L"Secondary UV set on the pirate On (toggle using u)\n");
		else g_pTxtHelper->DrawTextLine(L"Secondary UV set on the pirate Off (toggle using u)\n");

		// H
		if(g_iBumpFromHeightMapMethod==2)
			g_pTxtHelper->DrawTextLine(L"Bump from Height - 1 tap (toggle using h)\n");
		else if(g_iBumpFromHeightMapMethod==1)
			g_pTxtHelper->DrawTextLine(L"Bump from Height - 3 taps (toggle using h)\n");
		else 
			g_pTxtHelper->DrawTextLine(L"Bump from Height - HQ upscale (toggle using h)\n");


		// B
		if(g_iDecalBlendingMethod==2)
			g_pTxtHelper->DrawTextLine(L"Decals blending - todays approach uses decal direction (toggle using b)\n");
		else if(g_iDecalBlendingMethod==1) 
			g_pTxtHelper->DrawTextLine(L"Decals blending - surfgrad masked (toggle using b)\n");
		else 
			g_pTxtHelper->DrawTextLine(L"Decal blending - surfgrad additive (toggle using b)\n");


		// M
		if(g_bEnableDecalMipMapping)
			g_pTxtHelper->DrawTextLine(L"Decals mip mapping enabled (toggle using m)\n");
		else g_pTxtHelper->DrawTextLine(L"Decals mip mapping disabled (toggle using m)\n");


		// P
		if(g_bEnableDecals)
			g_pTxtHelper->DrawTextLine(L"Projected Decals enabled (toggle using p)\n");
		else g_pTxtHelper->DrawTextLine(L"Projected Decals disabled (toggle using p)\n");

		// N
		if(g_bShowNormalsWS)
			g_pTxtHelper->DrawTextLine(L"Show Normals in world space enabled (toggle using n)\n");
		else g_pTxtHelper->DrawTextLine(L"Show Normals in world space disabled (toggle using n)\n");

		// R
		if(g_bIndirectSpecular)
			g_pTxtHelper->DrawTextLine(L"Indirect Specular Reflection enabled (toggle using r)\n");
		else g_pTxtHelper->DrawTextLine(L"Indirect Specular Reflection disabled (toggle using r)\n");	

		// V
		if(g_bShowDebugVolumes)
			g_pTxtHelper->DrawTextLine(L"Show decal volumes in wireframe enabled (toggle using v)\n");
		else g_pTxtHelper->DrawTextLine(L"Show decal volumes in wireframe disabled (toggle using v)\n");

		// I
		if(g_bEnableShadows)
			g_pTxtHelper->DrawTextLine(L"Shadows enabled (toggle using i)\n");
		else g_pTxtHelper->DrawTextLine(L"Shadows disabled (toggle using i)\n");
#endif
		

#ifdef SHOW_DEMO_SCENE
#else
		if (g_iCullMethod == 0)
			g_pTxtHelper->DrawTextLine(L"Fine pruning disabled (toggle using m)\n");
		else
			g_pTxtHelper->DrawTextLine(L"Fine pruning enabled (toggle using m)\n");
#endif

		// O
		if (g_iVisualMode == 0)
			g_pTxtHelper->DrawTextLine(L"Show Decal Overlaps enabled (toggle using o)\n");
		else
			g_pTxtHelper->DrawTextLine(L"Show Decal Overlaps disabled (toggle using o)\n");
	}

	g_pTxtHelper->End();
}


float Lerp(const float fA, const float fB, const float fT) { return fA*(1-fT) + fB*fT; }


// full fov from left to right
void InitVolumeEntry(SFiniteVolumeData &lgtData, SFiniteVolumeBound &lgtColiData, const Mat44 &mat, const Vec3 vSize, const float range, const float fov, unsigned int uFlag)
{
	Mat33 rot;
	for(int c=0; c<3; c++)
	{
		const Vec4 col = GetColumn(mat, c);
		SetColumn(&rot, c, Vec3(col.x, col.y, col.z));
	}
	
	const Vec4 lastCol = GetColumn(mat, 3);
	const Vec3 vCen = Vec3(lastCol.x, lastCol.y, lastCol.z);

	float fFar = range;
	float fNear = 0.80*range;
	lgtData.vLpos = vCen;
	lgtData.fInvRange = -1/(fFar-fNear);
	lgtData.fNearRadiusOverRange_LP0 = fFar/(fFar-fNear);
	lgtData.fSphRadiusSq = fFar*fFar;
				

	float fFov = fov;		// full fov from left to right
	float fSeg = Lerp(2*range, 3*range, frnd());
	lgtData.fSegLength = (uFlag==WEDGE_VOLUME || uFlag==CAPSULE_VOLUME) ? fSeg : 0.0f;

	lgtData.uVolumeType = uFlag;


	Vec3 vX = GetColumn(rot, 0);
	Vec3 vY = GetColumn(rot, 1);
	Vec3 vZ = GetColumn(rot, 2);


	// default coli settings
	lgtColiData.vBoxAxisX = vX;
	lgtColiData.vBoxAxisY = vY;
	lgtColiData.vBoxAxisZ = vZ;
	lgtColiData.vScaleXZ = Vec2(1.0f, 1.0f);

	lgtData.vAxisX = vX;
	lgtData.vAxisY = vY;
	lgtData.vAxisZ = vZ;

	// build colision info for each volume type
	if(uFlag==CAPSULE_VOLUME)
	{
		lgtColiData.fRadius = range + 0.5*fSeg;
		lgtColiData.vCen = vCen + (0.5*fSeg)*lgtColiData.vBoxAxisX;
		lgtColiData.vBoxAxisX *= (range+0.5*fSeg); lgtColiData.vBoxAxisY *= range; lgtColiData.vBoxAxisZ *= range;

		lgtData.vCol = Vec3(1.0, 0.1, 1.0);
	}
	else if(uFlag==SPHERE_VOLUME)
	{
		lgtColiData.vBoxAxisX *= range; lgtColiData.vBoxAxisY *= range; lgtColiData.vBoxAxisZ *= range;
		lgtColiData.fRadius = range;
		lgtColiData.vCen = vCen;

		lgtData.vCol = Vec3(1,1,1);
	}
	else if(uFlag==SPOT_CIRCULAR_VOLUME || uFlag==WEDGE_VOLUME)
	{
		if(uFlag==SPOT_CIRCULAR_VOLUME)
			lgtData.vCol = Vec3(0*0.7,0.6,1);	
		else
			lgtData.vCol = Vec3(1,0.6,0*0.7);	

		float fQ = uFlag==WEDGE_VOLUME ? 0.1 : 1;
		
		//Vec3 vDir = Normalize( Vec3(fQ*0.5*(2*frnd()-1),  -1, fQ*0.5*(2*frnd()-1)) );
		
		lgtData.fPenumbra = cosf(fFov*0.5);
		lgtData.fInvUmbraDelta = 1/( lgtData.fPenumbra - cosf(0.02*(fFov*0.5)) );

		Vec3 vDir = -vY;

		//Vec3 vY = lgtColiData.vBoxAxisY;
		//Vec3 vTmpAxis = (fabsf(vY.x)<=fabsf(vY.y) && fabsf(vY.x)<=fabsf(vY.z)) ? Vec3(1,0,0) : ( fabsf(vY.y)<=fabsf(vY.z) ? Vec3(0,1,0) : Vec3(0,0,1) );
		//Vec3 vX = Normalize( Cross(vY,vTmpAxis ) );
		//lgtColiData.vBoxAxisZ = Cross(vX, vY);
		//lgtColiData.vBoxAxisX = vX;

		// this is silly but nevertheless where this is passed in engine (note the coliData is setup with vBoxAxisY==-vDir).
		
		// apply nonuniform scale to OBB of spot volume
		bool bSqueeze = true;//uFlag==SPOT_CIRCULAR_VOLUME && fFov<0.7*(M_PI*0.5f);

		float fS = bSqueeze ? tan(0.5*fFov) : sin(0.5*fFov);

		lgtColiData.vCen += (vCen + ((0.5f*range)*vDir) + ((0.5f*lgtData.fSegLength)*vX));

		lgtColiData.vBoxAxisX *= (fS*range + 0.5*lgtData.fSegLength);
		lgtColiData.vBoxAxisY *= (0.5f*range);
		lgtColiData.vBoxAxisZ *= (fS*range);

					

		float fAltDx = sin(0.5*fFov);
		float fAltDy = cos(0.5*fFov);
		fAltDy = fAltDy-0.5;
		if(fAltDy<0) fAltDy=-fAltDy;

		fAltDx *= range; fAltDy *= range;
		fAltDx += (0.5f*lgtData.fSegLength);

		float fAltDist = sqrt(fAltDy*fAltDy+fAltDx*fAltDx);
		lgtColiData.fRadius = fAltDist>(0.5*range) ? fAltDist : (0.5*range);

		if(bSqueeze)
			lgtColiData.vScaleXZ = Vec2(0.01f, 0.01f);

	}
	else if(uFlag==BOX_VOLUME)
	{
		//Mat33 rot; LoadRotation(&rot, 2*M_PI*frnd(), 2*M_PI*frnd(), 2*M_PI*frnd());
		float fSx = vSize.x;//5*2*(10*frnd()+4);
		float fSy = vSize.y;//5*2*(10*frnd()+4);
		float fSz = vSize.z;//5*2*(10*frnd()+4);

		//float fSx2 = 0.1f*fSx;
		//float fSy2 = 0.1f*fSy;
		//float fSz2 = 0.1f*fSz;
		float fSx2 = 0.85f*fSx;
		float fSy2 = 0.85f*fSy;
		float fSz2 = 0.85f*fSz;

		lgtColiData.vBoxAxisX = fSx*lgtData.vAxisX;
		lgtColiData.vBoxAxisY = fSy*lgtData.vAxisY;
		lgtColiData.vBoxAxisZ = fSz*lgtData.vAxisZ;

		lgtColiData.vCen = vCen;
		lgtColiData.fRadius = sqrtf(fSx*fSx+fSy*fSy+fSz*fSz);

		lgtData.vCol = Vec3(0.1,1,0.16);
		lgtData.fSphRadiusSq = lgtColiData.fRadius*lgtColiData.fRadius;

		lgtData.vBoxInnerDist = Vec3(fSx2, fSy2, fSz2);
		lgtData.vBoxInvRange = Vec3( 1/(fSx-fSx2), 1/(fSy-fSy2), 1/(fSz-fSz2) );
	}
}

#ifdef SHOW_DEMO_SCENE
void BuildVolumesBuffer()
{
	static bool bBufferMade = false;

	const float g_O = 2*2.59f;
	const float g_S = -1.0f;		// convert unity scene to right hand frame.

	if(!bBufferMade)
	{
		bBufferMade = true;

		int iNrLgts = 0;

		const float deg2rad = M_PI/180.0f;

		// full fov from left to right
		float totfov = 70.0f*deg2rad;

		// insert spot decal
		{ 
			SFiniteVolumeData &lgtData = g_sLgtData[iNrLgts];
			SFiniteVolumeBound &lgtColiData = g_sLgtColiData[iNrLgts];

			float totfov = 60.0f*deg2rad;

			const float range = 8.0f;

			Mat44 mat; LoadIdentity(&mat);
			LoadRotation(&mat, 55.0f*deg2rad, (180+105.0f)*deg2rad, 0.0f*deg2rad);
			SetColumn(&mat, 3, Vec3(g_S*3.39f+g_O-4, 1.28f+2.0f, -0.003f-2*0-1));
			InitVolumeEntry(g_sLgtData[iNrLgts], lgtColiData, mat, Vec3(0.0f, 0.0f, 0.0f), range, totfov, SPOT_CIRCULAR_VOLUME);
			//InitVolumeEntry(g_sLgtData[iNrLgts], lgtColiData, mat, Vec3(0.0f, 0.0f, 0.0f), range, totfov, WEDGE_VOLUME);
			++iNrLgts;
		}

		// insert sphere decal
		{ 
			SFiniteVolumeData &lgtData = g_sLgtData[iNrLgts];
			SFiniteVolumeBound &lgtColiData = g_sLgtColiData[iNrLgts];

			const float range = 1.6f;

			Mat44 mat; LoadIdentity(&mat);
			//LoadRotation(&mat, -30.0f*deg2rad, (180+105.0f)*deg2rad, 0.0f*deg2rad);
#if 1
			SetColumn(&mat, 3, Vec3(g_S*3.39f+g_O-2-0.7, 1.28f+0.5+0.2, -0.003f-2*0-0.3));
			InitVolumeEntry(g_sLgtData[iNrLgts], lgtColiData, mat, Vec3(0.0f, 0.0f, 0.0f), range, totfov, SPHERE_VOLUME);
#else
			SetColumn(&mat, 3, Vec3(g_S*3.39f+g_O-2-0.7-7, 1.28f+0.5+0.2, -0.003f-2*0-0.3));
			InitVolumeEntry(g_sLgtData[iNrLgts], lgtColiData, mat, Vec3(0.0f, 0.0f, 0.0f), 0.3*range, totfov, CAPSULE_VOLUME);
#endif
			++iNrLgts;
		}

		// insert box decal
		{ 
			SFiniteVolumeData &lgtData = g_sLgtData[iNrLgts];
			SFiniteVolumeBound &lgtColiData = g_sLgtColiData[iNrLgts];

			const Vec3 vSize = Vec3(0.5*1.2f,0.5*1.2f,1.7);
			const float range = Length(vSize);

			Mat44 mat; LoadIdentity(&mat);
			LoadRotation(&mat, -30.0f*deg2rad, (180+105.0f)*deg2rad, 0.0f*deg2rad);
			SetColumn(&mat, 3, Vec3(g_S*3.39f+g_O-1, 1.28f, -0.003f-2-1));
			//const Vec4 nose = -GetColumn(mat, 2);
			InitVolumeEntry(g_sLgtData[iNrLgts], lgtColiData, mat, vSize, range, totfov, BOX_VOLUME);
			++iNrLgts;
		}

	
	

		g_cVolumeTiler.InitTiler();

		g_iNrVolumes = iNrLgts;
	}
}
#else
void BuildVolumesBuffer()
{
	static bool bBufferMade = false;

	if(!bBufferMade)
	{
		bBufferMade = true;

		// build volume lists
		int iNrLgts = 0;
		int iRealCounter = 0;
		while(iNrLgts<g_iNrVolumes)
		{
			// 5 volume types define in 
			unsigned int uFlag = rand()%MAX_TYPES;

			const int iX = iNrLgts % g_iSqrtNrVolumes;
			const int iZ = iNrLgts / g_iSqrtNrVolumes;
			const float fX = 4000*(2*((iX+0.5f)/g_iSqrtNrVolumes)-1);
			const float fZ = 4000*(2*((iZ+0.5f)/g_iSqrtNrVolumes)-1);
			const float fY = g_cMesh.QueryTopY(fX, fZ)+12*5 + (uFlag==WEDGE_VOLUME || uFlag==CAPSULE_VOLUME ? 2*50 : 0);
			const Vec3 vCen = Vec3(fX, fY, fZ);

			const float fT = frnd();

			const float fRad = (uFlag==SPOT_CIRCULAR_VOLUME ? 3.0 : 2.0)*(100*fT + 80*(1-fT)+1);

			{
				SFiniteVolumeData &lgtData = g_sLgtData[iNrLgts];
				SFiniteVolumeBound &lgtColiData = g_sLgtColiData[iNrLgts];

				float fFar = fRad;
				float fNear = 0.80*fRad;
				lgtData.vLpos = vCen;
				lgtData.fInvRange = -1/(fFar-fNear);
				lgtData.fNearRadiusOverRange_LP0 = fFar/(fFar-fNear);
				lgtData.fSphRadiusSq = fFar*fFar;
				

				float fFov = 1.0*frnd()+0.2;		// full fov from left to right
				float fSeg = Lerp(fRad, 0.5*fRad, frnd());
				lgtData.fSegLength = (uFlag==WEDGE_VOLUME || uFlag==CAPSULE_VOLUME) ? fSeg : 0.0f;

				lgtData.uVolumeType = uFlag;


				lgtData.vAxisX = Vec3(1,0,0);
				lgtData.vAxisY = Vec3(0,1,0);
				lgtData.vAxisZ = Vec3(0,0,1);

				// default coli settings
				lgtColiData.vBoxAxisX = Vec3(1,0,0);
				lgtColiData.vBoxAxisY = Vec3(0,1,0);
				lgtColiData.vBoxAxisZ = Vec3(0,0,1);
				lgtColiData.vScaleXZ = Vec2(1.0f, 1.0f);

				// build colision info for each volume type
				if(uFlag==CAPSULE_VOLUME)
				{
					//lgtData.vLdir = lgtColiData.vBoxAxisX;
					lgtColiData.fRadius = fRad + 0.5*fSeg;
					lgtColiData.vCen = vCen + (0.5*fSeg)*lgtColiData.vBoxAxisX;
					lgtColiData.vBoxAxisX *= (fRad+0.5*fSeg); lgtColiData.vBoxAxisY *= fRad; lgtColiData.vBoxAxisZ *= fRad;

					lgtData.vCol = Vec3(1.0, 0.1, 1.0);
				}
				else if(uFlag==SPHERE_VOLUME)
				{
					lgtColiData.vBoxAxisX *= fRad; lgtColiData.vBoxAxisY *= fRad; lgtColiData.vBoxAxisZ *= fRad;
					lgtColiData.fRadius = fRad;
					lgtColiData.vCen = vCen;

					lgtData.vCol = Vec3(1,1,1);
				}
				else if(uFlag==SPOT_CIRCULAR_VOLUME || uFlag==WEDGE_VOLUME)
				{
					if(uFlag==SPOT_CIRCULAR_VOLUME)
						lgtData.vCol = Vec3(0*0.7,0.6,1);	
					else
						lgtData.vCol = Vec3(1,0.6,0*0.7);	
					fFov *= 2;

					float fQ = uFlag==WEDGE_VOLUME ? 0.1 : 1;
					Vec3 vDir = Normalize( Vec3(fQ*0.5*(2*frnd()-1),  -1, fQ*0.5*(2*frnd()-1)) );
					//lgtData.vBoxAxisX = vDir;		// Spot Dir
					lgtData.fPenumbra = cosf(fFov*0.5);
					lgtData.fInvUmbraDelta = 1/( lgtData.fPenumbra - cosf(0.02*(fFov*0.5)) );

					lgtColiData.vBoxAxisY = -vDir;

					Vec3 vY = lgtColiData.vBoxAxisY;
					Vec3 vTmpAxis = (fabsf(vY.x)<=fabsf(vY.y) && fabsf(vY.x)<=fabsf(vY.z)) ? Vec3(1,0,0) : ( fabsf(vY.y)<=fabsf(vY.z) ? Vec3(0,1,0) : Vec3(0,0,1) );
					Vec3 vX = Normalize( Cross(vY,vTmpAxis ) );
					lgtColiData.vBoxAxisZ = Cross(vX, vY);
					lgtColiData.vBoxAxisX = vX;

					// this is silly but nevertheless where this is passed in engine (note the coliData is setup with vBoxAxisY==-vDir).
					lgtData.vAxisX = vX;
					lgtData.vAxisY = vY;
					lgtData.vAxisZ = Cross(vX, vY);

					// apply nonuniform scale to OBB of spot volume
					bool bSqueeze = true;//uFlag==SPOT_CIRCULAR_VOLUME && fFov<0.7*(M_PI*0.5f);

					float fS = bSqueeze ? tan(0.5*fFov) : sin(0.5*fFov);

					lgtColiData.vCen += (vCen + ((0.5f*fRad)*vDir) + ((0.5f*lgtData.fSegLength)*vX));

					lgtColiData.vBoxAxisX *= (fS*fRad + 0.5*lgtData.fSegLength);
					lgtColiData.vBoxAxisY *= (0.5f*fRad);
					lgtColiData.vBoxAxisZ *= (fS*fRad);

					

					float fAltDx = sin(0.5*fFov);
					float fAltDy = cos(0.5*fFov);
					fAltDy = fAltDy-0.5;
					if(fAltDy<0) fAltDy=-fAltDy;

					fAltDx *= fRad; fAltDy *= fRad;
					fAltDx += (0.5f*lgtData.fSegLength);

					float fAltDist = sqrt(fAltDy*fAltDy+fAltDx*fAltDx);
					lgtColiData.fRadius = fAltDist>(0.5*fRad) ? fAltDist : (0.5*fRad);

					if(bSqueeze)
						lgtColiData.vScaleXZ = Vec2(0.01f, 0.01f);

				}
				else if(uFlag==BOX_VOLUME)
				{
					Mat33 rot; LoadRotation(&rot, 2*M_PI*frnd(), 2*M_PI*frnd(), 2*M_PI*frnd());
					float fSx = 5*2*(10*frnd()+4);
					float fSy = 5*2*(10*frnd()+4);
					float fSz = 5*2*(10*frnd()+4);

					float fSx2 = 0.1f*fSx;
					float fSy2 = 0.1f*fSy;
					float fSz2 = 0.1f*fSz;

					lgtData.vAxisX = GetColumn(rot, 0);
					lgtData.vAxisY = GetColumn(rot, 1);
					lgtData.vAxisZ = GetColumn(rot, 2);

					lgtColiData.vBoxAxisX = fSx*lgtData.vAxisX;
					lgtColiData.vBoxAxisY = fSy*lgtData.vAxisY;
					lgtColiData.vBoxAxisZ = fSz*lgtData.vAxisZ;

					lgtColiData.vCen = vCen;
					lgtColiData.fRadius = sqrtf(fSx*fSx+fSy*fSy+fSz*fSz);

					lgtData.vCol = Vec3(0.1,1,0.16);
					lgtData.fSphRadiusSq = lgtColiData.fRadius*lgtColiData.fRadius;

					lgtData.vBoxInnerDist = Vec3(fSx2, fSy2, fSz2);
					lgtData.vBoxInvRange = Vec3( 1/(fSx-fSx2), 1/(fSy-fSy2), 1/(fSz-fSz2) );
				}

				

				++iNrLgts;
			}
		}
	

		g_cVolumeTiler.InitTiler();
	}
}
#endif

void RenderDebugVolumes(ID3D11DeviceContext* pd3dImmediateContext)
{
	if(g_iNrVolumes>0)
	{
		CShaderPipeline &pipe = debug_volume_shader_pipeline;

		pipe.PrepPipelineForRendering(pd3dImmediateContext);
	

		// set streams and layout
		pd3dImmediateContext->IASetVertexBuffers( 0, 0, NULL, NULL, NULL );
		pd3dImmediateContext->IASetIndexBuffer( NULL, DXGI_FORMAT_UNKNOWN, 0 );
		pd3dImmediateContext->IASetInputLayout( NULL );

		// Set primitive topology
		pd3dImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_LINELIST );
		pd3dImmediateContext->DrawInstanced( 2*MAX_NR_SEGMENTS_ON_ANY_VOLUME, g_iNrVolumes, 0, 0);
		//pd3dImmediateContext->Draw( 2*12, 0);
		pipe.FlushResources(pd3dImmediateContext);
	}
}

void render_surface(ID3D11DeviceContext* pd3dImmediateContext, bool bSimpleLayout)
{
#ifdef SHOW_DEMO_SCENE
	RenderSceneGraph(pd3dImmediateContext, bSimpleLayout, g_bEnableDecals, g_bEnableDecalMipMapping, false);
#else
	CShaderPipeline &shader_pipe = bSimpleLayout ? shader_dpthfill_pipeline : shader_pipeline;

	shader_pipe.PrepPipelineForRendering(pd3dImmediateContext);
	

	// set streams and layout
	UINT stride = sizeof(SFilVert), offset = 0;
	pd3dImmediateContext->IASetVertexBuffers( 0, 1, g_cMesh.GetVertexBuffer(), &stride, &offset );
	pd3dImmediateContext->IASetIndexBuffer( g_cMesh.GetIndexBuffer(), DXGI_FORMAT_R32_UINT, 0 );
	pd3dImmediateContext->IASetInputLayout( bSimpleLayout ? g_pVertexSimpleLayout : g_pVertexLayout );

	// Set primitive topology
	pd3dImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	pd3dImmediateContext->DrawIndexed( 3*g_cMesh.GetNrTriangles(), 0, 0 );
	shader_pipe.FlushResources(pd3dImmediateContext);
#endif

	if(!bSimpleLayout && g_bShowDebugVolumes) RenderDebugVolumes(pd3dImmediateContext);
}


Mat44 g_m44Proj, g_m44InvProj, g_mViewToScr, g_mScrToView;


Vec3 XMVToVec3(const DirectX::XMVECTOR vec)
{
	return Vec3(DirectX::XMVectorGetX(vec), DirectX::XMVectorGetY(vec), DirectX::XMVectorGetZ(vec));
}

Vec4 XMVToVec4(const DirectX::XMVECTOR vec)
{
	return Vec4(DirectX::XMVectorGetX(vec), DirectX::XMVectorGetY(vec), DirectX::XMVectorGetZ(vec), DirectX::XMVectorGetW(vec));
}

Mat44 ToMat44(const DirectX::XMMATRIX &dxmat)
{
	Mat44 res;

	for (int c = 0; c < 4; c++)
		SetColumn(&res, c, XMVToVec4(dxmat.r[c]));

	return res;
}


void CALLBACK OnD3D11FrameRender( ID3D11Device* pd3dDevice, ID3D11DeviceContext* pd3dImmediateContext, 
                                  double fTime, float fElapsedTime, void* pUserContext )
{
	HRESULT hr;

	//const float fTimeDiff = DXUTGetElapsedTime();

	// clear screen
    ID3D11RenderTargetView* pRTV = DXUTGetD3D11RenderTargetView();
	ID3D11DepthStencilView* pDSV = g_tex_depth.GetDSV();//DXUTGetD3D11DepthStencilView();
	//DXUTGetD3D11DepthStencil();

	
	Vec3 vToPoint = XMVToVec3(g_Camera.GetLookAtPt());

	Vec3 cam_pos = XMVToVec3(g_Camera.GetEyePt());
    Mat44 world_to_view = ToMat44(g_Camera.GetViewMatrix() );	// get world to view projection

	Mat44 mZflip; LoadIdentity(&mZflip);
	SetColumn(&mZflip, 2, Vec4(0,0,-1,0));
#ifndef LEFT_HAND_COORDINATES
	world_to_view = mZflip * world_to_view * mZflip;
#else
	world_to_view = world_to_view * mZflip;
#endif
	
	Mat44 m44LocalToWorld; LoadIdentity(&m44LocalToWorld);
	Mat44 m44LocalToView = world_to_view * m44LocalToWorld;
	Mat44 Trans = g_m44Proj * world_to_view;

	
	

	// fill constant buffers
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
#ifndef SHOW_DEMO_SCENE
	V( pd3dImmediateContext->Map( g_pMeshInstanceCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMeshInstance *)MappedSubResource.pData)->g_mLocToWorld = Transpose(m44LocalToWorld); 
	((cbMeshInstance *)MappedSubResource.pData)->g_mWorldToLocal = Transpose(~m44LocalToWorld); 
    pd3dImmediateContext->Unmap( g_pMeshInstanceCB, 0 );
#endif

	// prefill shadow map
#ifdef SHOW_DEMO_SCENE
	if(g_bEnableShadows) g_shadowMap.RenderShadowMap(pd3dImmediateContext, g_pGlobalsCB, GetSunDir());
#endif


	// fill constant buffers
	const Mat44 view_to_world = ~world_to_view;
	V( pd3dImmediateContext->Map( g_pGlobalsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbGlobals *)MappedSubResource.pData)->g_mWorldToView = Transpose(world_to_view);
	((cbGlobals *)MappedSubResource.pData)->g_mViewToWorld = Transpose(view_to_world);
	((cbGlobals *)MappedSubResource.pData)->g_mScrToView = Transpose(g_mScrToView);
	((cbGlobals *)MappedSubResource.pData)->g_mProj = Transpose(g_m44Proj);
	((cbGlobals *)MappedSubResource.pData)->g_mViewProjection = Transpose(Trans);
	((cbGlobals *)MappedSubResource.pData)->g_vCamPos = view_to_world * Vec3(0,0,0);
	((cbGlobals *)MappedSubResource.pData)->g_iWidth = DXUTGetDXGIBackBufferSurfaceDesc()->Width;
	((cbGlobals *)MappedSubResource.pData)->g_iHeight = DXUTGetDXGIBackBufferSurfaceDesc()->Height;
	((cbGlobals *)MappedSubResource.pData)->g_iMode = g_iVisualMode;
	((cbGlobals *)MappedSubResource.pData)->g_iDecalBlendingMethod = g_iDecalBlendingMethod;
	((cbGlobals *)MappedSubResource.pData)->g_bShowNormalsWS = g_bShowNormalsWS;
	((cbGlobals *)MappedSubResource.pData)->g_bIndirectSpecular = g_bIndirectSpecular;
	((cbGlobals *)MappedSubResource.pData)->g_vSunDir = GetSunDir();
	((cbGlobals *)MappedSubResource.pData)->g_bEnableShadows = g_bEnableShadows;
	((cbGlobals *)MappedSubResource.pData)->g_iBumpFromHeightMapMethod = g_iBumpFromHeightMapMethod;
	((cbGlobals *)MappedSubResource.pData)->g_bUseSecondaryUVsetOnPirate = g_bUseSecondaryUVsetOnPirate;
    pd3dImmediateContext->Unmap( g_pGlobalsCB, 0 );

	V( pd3dImmediateContext->Map( g_pVolumeClipInfo, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbBoundsInfo *) MappedSubResource.pData)->g_mProjection = Transpose(g_m44Proj);
	((cbBoundsInfo *) MappedSubResource.pData)->g_mInvProjection = Transpose(g_m44InvProj);
	((cbBoundsInfo *) MappedSubResource.pData)->g_mScrProjection = Transpose(g_mViewToScr);
	((cbBoundsInfo *) MappedSubResource.pData)->g_mInvScrProjection = Transpose(g_mScrToView);
	((cbBoundsInfo *) MappedSubResource.pData)->g_iNrVisibVolumes = g_iNrVolumes;
	((cbBoundsInfo *) MappedSubResource.pData)->g_vStuff = Vec3(0,0,0);
	pd3dImmediateContext->Unmap( g_pVolumeClipInfo, 0 );

	// build volume list
	g_cVolumeTiler.InitFrame(world_to_view, g_m44Proj);
	for(int l=0; l<g_iNrVolumes; l++)
	{
		g_cVolumeTiler.AddVolume(g_sLgtData[l], g_sLgtColiData[l]);
	}
	g_cVolumeTiler.CompileVolumeList();


	// transfer volume bounds
	V( pd3dImmediateContext->Map( g_OrientedBounds.GetStagedBuffer(), 0, D3D11_MAP_WRITE, 0, &MappedSubResource ) );
	for(int l=0; l<g_iNrVolumes; l++)
	{
		((SFiniteVolumeBound *) MappedSubResource.pData)[l] = g_cVolumeTiler.GetOrderedBoundsList()[l];
	}
	pd3dImmediateContext->Unmap( g_OrientedBounds.GetStagedBuffer(), 0 );

	V( pd3dImmediateContext->Map( g_VolumeDataBuffer.GetStagedBuffer(), 0, D3D11_MAP_WRITE, 0, &MappedSubResource ) );
	for(int l=0; l<g_iNrVolumes; l++)
	{
		((SFiniteVolumeData *) MappedSubResource.pData)[l] = g_cVolumeTiler.GetVolumesDataList()[l];
		if(g_iVisualMode==0) ((SFiniteVolumeData *) MappedSubResource.pData)[l].vCol = Vec3(1,1,1);
	}
	pd3dImmediateContext->Unmap( g_VolumeDataBuffer.GetStagedBuffer(), 0 );

	pd3dImmediateContext->CopyResource(g_VolumeDataBuffer.GetBuffer(), g_VolumeDataBuffer.GetStagedBuffer());


	// Convert OBBs of volumes into screen space AABBs (incl. depth)
	const int nrGroups = (g_iNrVolumes*8 + 63)/64;
	ID3D11UnorderedAccessView* g_ppNullUAV[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

	ID3D11Buffer * pDatas[] = {g_pVolumeClipInfo};
	pd3dImmediateContext->CSSetConstantBuffers(0, 1, pDatas);		// 

	pd3dImmediateContext->CopyResource(g_OrientedBounds.GetBuffer(), g_OrientedBounds.GetStagedBuffer());
	ID3D11ShaderResourceView * pSRVbounds[] = {g_OrientedBounds.GetSRV()};
	pd3dImmediateContext->CSSetShaderResources(0, 1, pSRVbounds);

	ID3D11UnorderedAccessView * ppAABB_UAV[] = { g_ScrSpaceAABounds.GetUAV() };
	pd3dImmediateContext->CSSetUnorderedAccessViews(0, 1, ppAABB_UAV, 0);
	ID3D11ComputeShader * pShaderCS = (ID3D11ComputeShader *) scrbound_shader.GetDeviceChild();
	pd3dImmediateContext->CSSetShader( pShaderCS, NULL, 0 );
	pd3dImmediateContext->Dispatch(nrGroups, 1, 1);
	pd3dImmediateContext->CSSetUnorderedAccessViews(0, 1, g_ppNullUAV, 0);

	ID3D11ShaderResourceView * pNullSRV_bound[] = {NULL};
	pd3dImmediateContext->CSSetShaderResources(0, 1, pNullSRV_bound);

	// debugging code...
	/*
	pd3dImmediateContext->CopyResource(g_pScrSpaceAABounds_staged, g_pScrSpaceAABounds);
	V( pd3dImmediateContext->Map( g_pScrSpaceAABounds_staged, 0, D3D11_MAP_READ, 0, &MappedSubResource ) );
	const Vec3 * pData0 = ((Vec3 *) MappedSubResource.pData);
	const Vec3 * pData1 = g_cVolumeTiler.GetScrBoundsList();
	pd3dImmediateContext->Unmap( g_pScrSpaceAABounds_staged, 0 );*/
	

	// prefill depth
	const bool bRenderFront = true;
	float ClearColor[4] = { 0.03f, 0.05f, 0.1f, 0.0f };

	DXUTSetupD3D11Views(pd3dImmediateContext);

	pd3dImmediateContext->OMSetRenderTargets( 0, NULL, pDSV );
    pd3dImmediateContext->ClearDepthStencilView( pDSV, D3D11_CLEAR_DEPTH, 1.0f, 0 );


	pd3dImmediateContext->RSSetState( GetDefaultRasterSolidCullBack()  );
	pd3dImmediateContext->OMSetDepthStencilState( GetDefaultDepthStencilState(), 0 );

	render_surface(pd3dImmediateContext, true);

	// resolve shadow map
#ifdef SHOW_DEMO_SCENE
	if(g_bEnableShadows) g_shadowMap.ResolveToScreen(pd3dImmediateContext, g_tex_depth.GetReadOnlyDSV(), g_pGlobalsCB);
#endif

	// restore depth state
	pd3dImmediateContext->OMSetDepthStencilState( GetDefaultDepthStencilState_NoDepthWrite(), 0 );

	// switch to back-buffer
	pd3dImmediateContext->OMSetRenderTargets( 1, &pRTV, g_tex_depth.GetReadOnlyDSV() );
	pd3dImmediateContext->ClearRenderTargetView( pRTV, ClearColor );

	const int iNrTilesX = (DXUTGetDXGIBackBufferSurfaceDesc()->Width+15)/16;
	const int iNrTilesY = (DXUTGetDXGIBackBufferSurfaceDesc()->Height+15)/16;
	ID3D11ShaderResourceView * pNullSRV[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

	// build a volume list per 16x16 tile
	ID3D11UnorderedAccessView * ppVolumeList_UAV[] = { g_volumeListBuffer.GetUAV() };
	ID3D11ShaderResourceView * pSRV[] = {g_tex_depth.GetSRV(), g_ScrSpaceAABounds.GetSRV(), g_VolumeDataBuffer.GetSRV(), g_OrientedBounds.GetSRV()};


	pShaderCS = (ID3D11ComputeShader *) (g_iCullMethod==0 ? volumelist_coarse_shader.GetDeviceChild() : volumelist_exact_shader.GetDeviceChild());
	pd3dImmediateContext->CSSetUnorderedAccessViews(0, 1, ppVolumeList_UAV, 0);
	pd3dImmediateContext->CSSetShaderResources(0, 4, pSRV);
	pd3dImmediateContext->CSSetShader( pShaderCS, NULL, 0 );
	pd3dImmediateContext->Dispatch(iNrTilesX, iNrTilesY, 1);
	pd3dImmediateContext->CSSetUnorderedAccessViews(0, 1, g_ppNullUAV, 0);
	pd3dImmediateContext->CSSetShaderResources(0, 4, pNullSRV);

	// debugging code...
	/*
	pd3dImmediateContext->CopyResource(g_pVolumeListBuffer_staged, g_pVolumeListBuffer);
	V( pd3dImmediateContext->Map( g_pVolumeListBuffer_staged, 0, D3D11_MAP_READ, 0, &MappedSubResource ) );
	const unsigned int * pData2 = ((const unsigned int *) MappedSubResource.pData);
	pd3dImmediateContext->Unmap( g_pVolumeListBuffer_staged, 0 );	
	*/

#ifdef SHOW_DEMO_SCENE
	//
	g_canvas.DrawCanvas(pd3dImmediateContext, g_pGlobalsCB);

	// restore depth state
	pd3dImmediateContext->OMSetDepthStencilState( GetDefaultDepthStencilState_NoDepthWrite(), 0 );
#endif

	
	// Do tiled forward rendering
	render_surface(pd3dImmediateContext, false);



	// fire off menu text
	RenderText();
}


int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow )
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

	 
    // Set DXUT callbacks
    DXUTSetCallbackDeviceChanging( ModifyDeviceSettings );
    DXUTSetCallbackMsgProc( MsgProc );
    DXUTSetCallbackFrameMove( OnFrameMove );

    DXUTSetCallbackD3D11DeviceAcceptable( IsD3D11DeviceAcceptable );
    DXUTSetCallbackD3D11DeviceCreated( OnD3D11CreateDevice );
    DXUTSetCallbackD3D11SwapChainResized( OnD3D11ResizedSwapChain );
    DXUTSetCallbackD3D11FrameRender( OnD3D11FrameRender );
    DXUTSetCallbackD3D11SwapChainReleasing( OnD3D11ReleasingSwapChain );
    DXUTSetCallbackD3D11DeviceDestroyed( OnD3D11DestroyDevice );

    DXUTSetCallbackKeyboard( OnKeyboard );

    InitApp();
    DXUTInit( true, true );
    DXUTSetCursorSettings( true, true ); // Show the cursor and clip it when in full screen
    DXUTCreateWindow( L"Surface Gradient Based Bump Mapping Demo." );
	int dimX = 1280, dimY = 960;
	DXUTCreateDevice( D3D_FEATURE_LEVEL_11_0, true, dimX, dimY);
    //DXUTCreateDevice( D3D_FEATURE_LEVEL_11_0, true, 1024, 768);
    DXUTMainLoop(); // Enter into the DXUT render loop

    return DXUTGetExitCode();
}



//--------------------------------------------------------------------------------------
// Create any D3D11 resources that depend on the back buffer
//--------------------------------------------------------------------------------------


HRESULT CALLBACK OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain,
                                          const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext )
{
    HRESULT hr;


    // Setup the camera's projection parameters
	int w = pBackBufferSurfaceDesc->Width;
	int h = pBackBufferSurfaceDesc->Height;

#ifdef SHOW_DEMO_SCENE
	const float scale = 0.01f;
#else
	const float scale = 1.0f;	// 0.1f
#endif

	//const float fFov = 30;
	const float fNear = 10 * scale;
	const float fFar = 10000 * scale;
	//const float fNear = 45;//275;
	//const float fFar = 65;//500;
	//const float fHalfWidthAtMinusNear = fNear * tanf((fFov*((float) M_PI))/360);
	//const float fHalfHeightAtMinusNear = fHalfWidthAtMinusNear * (((float) 3)/4.0);

	const float fFov = 2*23;
	const float fHalfHeightAtMinusNear = fNear * tanf((fFov*((float) M_PI))/360);
	const float fHalfWidthAtMinusNear = fHalfHeightAtMinusNear * (((float) w)/h);
	

	const float fS = 1.0;// 1280.0f / 960.0f;

	myFrustum(g_m44Proj.m_fMat, -fS*fHalfWidthAtMinusNear, fS*fHalfWidthAtMinusNear, -fHalfHeightAtMinusNear, fHalfHeightAtMinusNear, fNear, fFar);


	{
		float fAspectRatio = fS;
		g_Camera.SetProjParams( (fFov*M_PI)/360, fAspectRatio, fNear, fFar );
	}


	Mat44 mToScr;
	SetRow(&mToScr, 0, Vec4(0.5*w, 0,     0,  0.5*w));
	SetRow(&mToScr, 1, Vec4(0,     -0.5*h, 0,  0.5*h));
	SetRow(&mToScr, 2, Vec4(0,     0,     1,  0));
	SetRow(&mToScr, 3, Vec4(0,     0,     0,  1));

	g_mViewToScr = mToScr * g_m44Proj;
	g_mScrToView = ~g_mViewToScr;
	g_m44InvProj = ~g_m44Proj;

	



    // Set GUI size and locations
    /*g_HUD.SetLocation( pBackBufferSurfaceDesc->Width - 170, 0 );
    g_HUD.SetSize( 170, 170 );
    g_SampleUI.SetLocation( pBackBufferSurfaceDesc->Width - 245, pBackBufferSurfaceDesc->Height - 520 );
    g_SampleUI.SetSize( 245, 520 );*/

	// create render targets
	const bool bEnableReadBySampling = true;
	const bool bEnableWriteTo = true;
	const bool bAllocateMipMaps = false;
	const bool bAllowStandardMipMapGeneration = false;
	const void * pInitData = NULL;

	g_tex_depth.CleanUp();

	g_tex_depth.CreateTexture(pd3dDevice,w,h, DXGI_FORMAT_R24G8_TYPELESS, bAllocateMipMaps, false, NULL,
								bEnableReadBySampling, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, bEnableWriteTo, DXGI_FORMAT_D24_UNORM_S8_UINT,
								true);


	////////////////////////////////////////////////
	g_volumeListBuffer.CleanUp();



	const int nrTiles = ((w+15)/16)*((h+15)/16);
	g_volumeListBuffer.CreateBuffer(pd3dDevice, NR_USHORTS_PER_TILE*sizeof( unsigned short ) * nrTiles, 0, NULL, CBufferObject::DefaultBuf, true, true, CBufferObject::StagingCpuReadOnly);
	g_volumeListBuffer.AddTypedSRV(pd3dDevice, DXGI_FORMAT_R16_UINT);
	g_volumeListBuffer.AddTypedUAV(pd3dDevice, DXGI_FORMAT_R16_UINT);


#ifndef SHOW_DEMO_SCENE
	shader_pipeline.RegisterResourceView("g_vVolumeList", g_volumeListBuffer.GetSRV());
#endif
	
#ifdef SHOW_DEMO_SCENE
	g_shadowMap.OnResize(pd3dDevice, g_tex_depth.GetSRV());
	PassVolumeIndicesPerTileBuffer(g_volumeListBuffer.GetSRV(), g_shadowMap.GetShadowResolveSRV());
#endif

	////////////////////////////////////////////////
	V_RETURN( g_DialogResourceManager.OnD3D11ResizedSwapChain( pd3dDevice, pBackBufferSurfaceDesc ) );
	//V_RETURN( g_D3DSettingsDlg.OnD3D11ResizedSwapChain( pd3dDevice, pBackBufferSurfaceDesc ) );

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11ResizedSwapChain 
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11ReleasingSwapChain( void* pUserContext )
{
    g_DialogResourceManager.OnD3D11ReleasingSwapChain();
}


//--------------------------------------------------------------------------------------
// Handle key presses
//--------------------------------------------------------------------------------------
LRESULT CALLBACK MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool* pbNoFurtherProcessing,
                          void* pUserContext )
{
#if 0
    switch( uMsg )
    {
        case WM_KEYDOWN:    // Prevent the camera class to use some prefefined keys that we're already using    
		{
            switch( (UINT)wParam )
            {
                case VK_CONTROL:    
                case VK_LEFT:
				{
					g_fL0ay -= 0.05f;
					return 0;
				}
				break;
                case VK_RIGHT:         
				{
					g_fL0ay += 0.05f;
					return 0;
				}
                case VK_UP:
				{
					g_fL0ax += 0.05f;
					if(g_fL0ax>(M_PI/2.5)) g_fL0ax=(M_PI/2.5);
					return 0;
				}
				break;
                case VK_DOWN:
				{
					g_fL0ax -= 0.05f;
					if(g_fL0ax<-(M_PI/2.5)) g_fL0ax=-(M_PI/2.5);
					return 0;
				}
				break;
				case 'F':
				{
					int iTing;
					iTing = 0;
				}
				default:
					;
            }
		}
		break;
    }
#endif
    // Pass all remaining windows messages to camera so it can respond to user input
    g_Camera.HandleMessages( hWnd, uMsg, wParam, lParam );


    return 0;
}

void CALLBACK OnKeyboard( UINT nChar, bool bKeyDown, bool bAltDown, void* pUserContext )
{
	if(bKeyDown)
	{
		if(nChar=='M')
		{
#ifndef SHOW_DEMO_SCENE
			g_iCullMethod = 1-g_iCullMethod;
#else
			g_bEnableDecalMipMapping = !g_bEnableDecalMipMapping;
#endif
		}

		if(nChar=='O')
		{
			g_iVisualMode = 1-g_iVisualMode;
		}

		if (nChar == 'X')
		{
			g_iMenuVisib = 1 - g_iMenuVisib;
		}

		if (nChar == 'B')
		{
			g_iDecalBlendingMethod = g_iDecalBlendingMethod+1;
			if(g_iDecalBlendingMethod>=3) g_iDecalBlendingMethod -= 3;
		}

		if (nChar == 'H')
		{
			g_iBumpFromHeightMapMethod = g_iBumpFromHeightMapMethod+1;
			if(g_iBumpFromHeightMapMethod>=3) g_iBumpFromHeightMapMethod -= 3;
		}

		if (nChar == 'P')
		{
			g_bEnableDecals = !g_bEnableDecals;
		}

		if (nChar == 'N')
		{
			g_bShowNormalsWS = !g_bShowNormalsWS;
		}

		if (nChar == 'R')
		{
			g_bIndirectSpecular = !g_bIndirectSpecular;
		}

		if (nChar == 'V')
		{
			g_bShowDebugVolumes = !g_bShowDebugVolumes;
		}

		if (nChar == 'I')
		{
			g_bEnableShadows = !g_bEnableShadows;
		}

		if (nChar == 'U')
		{
			g_bUseSecondaryUVsetOnPirate = !g_bUseSecondaryUVsetOnPirate;
		}

		

	}
}


//--------------------------------------------------------------------------------------
// Called right before creating a D3D9 or D3D10 device, allowing the app to modify the device settings as needed
//--------------------------------------------------------------------------------------
bool CALLBACK ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings, void* pUserContext )
{
    // For the first device created if its a REF device, optionally display a warning dialog box
    static bool s_bFirstTime = true;
    if( s_bFirstTime )
    {
		s_bFirstTime = false;
		pDeviceSettings->d3d11.AutoCreateDepthStencil = false;

		/*
		s_bFirstTime = false;
        if( ( DXUT_D3D11_DEVICE == pDeviceSettings->ver &&
              pDeviceSettings->d3d11.DriverType == D3D_DRIVER_TYPE_REFERENCE ) )
        {
            DXUTDisplaySwitchingToREFWarning( pDeviceSettings->ver );
        }

        // Enable 4xMSAA by default
        DXGI_SAMPLE_DESC MSAA4xSampleDesc = { 4, 0 };
        pDeviceSettings->d3d11.sd.SampleDesc = MSAA4xSampleDesc;*/
    }

    return true;
}


//--------------------------------------------------------------------------------------
// Handle updates to the scene
//--------------------------------------------------------------------------------------
void CALLBACK OnFrameMove( double fTime, float fElapsedTime, void* pUserContext )
{
    // Update the camera's position based on user input 
    g_Camera.FrameMove( fElapsedTime );
}

//--------------------------------------------------------------------------------------
// Reject any D3D11 devices that aren't acceptable by returning false
//--------------------------------------------------------------------------------------
bool CALLBACK IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output, const CD3D11EnumDeviceInfo *DeviceInfo,
                                       DXGI_FORMAT BackBufferFormat, bool bWindowed, void* pUserContext )
{
    return true;
}


//--------------------------------------------------------------------------------------
// Create any D3D11 resources that aren't dependant on the back buffer
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc,
                                      void* pUserContext )
{
    HRESULT hr;

    // Get device context
    ID3D11DeviceContext* pd3dImmediateContext = DXUTGetD3D11DeviceContext();


	// create text helper
	V_RETURN( g_DialogResourceManager.OnD3D11CreateDevice(pd3dDevice, pd3dImmediateContext) );
	g_pTxtHelper = new CDXUTTextHelper( pd3dDevice, pd3dImmediateContext, &g_DialogResourceManager, 15 );

	InitUtils(pd3dDevice);

	// set compiler flag
	DWORD dwShaderFlags = D3D10_SHADER_ENABLE_STRICTNESS;
#if defined( DEBUG ) || defined( _DEBUG )
    // Set the D3D10_SHADER_DEBUG flag to embed debug information in the shaders.
    // Setting this flag improves the shader debugging experience, but still allows 
    // the shaders to be optimized and to run exactly the way they will run in 
    // the release configuration of this program.
    dwShaderFlags |= D3D10_SHADER_DEBUG;
#endif
	//dwShaderFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL0;

	// create constant buffers
    D3D11_BUFFER_DESC bd;

#ifndef SHOW_DEMO_SCENE
	memset(&bd, 0, sizeof(bd));
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = (sizeof( cbMeshInstance )+0xf)&(~0xf);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags = 0;
    V_RETURN( pd3dDevice->CreateBuffer( &bd, NULL, &g_pMeshInstanceCB ) );
#endif


	memset(&bd, 0, sizeof(bd));
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = (sizeof( cbGlobals )+0xf)&(~0xf);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags = 0;
    V_RETURN( pd3dDevice->CreateBuffer( &bd, NULL, &g_pGlobalsCB ) );

	


	CONST D3D10_SHADER_MACRO* pDefines = NULL;

	// compile per tile volume list generation compute shader (with and without fine pruning)
	CONST D3D10_SHADER_MACRO sDefineExact[] = {{"FINE_PRUNING_ENABLED", NULL}, {NULL, NULL}};
	volumelist_exact_shader.CompileShaderFunction(pd3dDevice, L"volumelist_cs.hlsl", sDefineExact, "main", "cs_5_0", dwShaderFlags );
	volumelist_coarse_shader.CompileShaderFunction(pd3dDevice, L"volumelist_cs.hlsl", pDefines, "main", "cs_5_0", dwShaderFlags );

	// compile compute shader for screen-space AABB generation
#ifdef RECOMPILE_SCRBOUND_CS_SHADER
	scrbound_shader.CompileShaderFunction(pd3dDevice, L"scrbound_cs.hlsl", pDefines, "main", "cs_5_0", dwShaderFlags );
	FILE * fptr_out = fopen("scrbound_cs.bsh", "wb");
	fwrite(scrbound_shader.GetBufferPointer(), 1, scrbound_shader.GetBufferSize(), fptr_out);
	fclose(fptr_out);
#else
	scrbound_shader.CreateComputeShaderFromBinary(pd3dDevice, "scrbound_cs.bsh");
#endif

#ifndef SHOW_DEMO_SCENE
	// compile tiled forward lighting shader
	vert_shader.CompileShaderFunction(pd3dDevice, L"shader_lighting_old_fptl_demo.hlsl", pDefines, "RenderSceneVS", "vs_5_0", dwShaderFlags );
	pix_shader.CompileShaderFunction(pd3dDevice, L"shader_lighting_old_fptl_demo.hlsl", pDefines, "RenderScenePS", "ps_5_0", dwShaderFlags );


	// prepare shader pipeline
	shader_pipeline.SetVertexShader(&vert_shader);
	shader_pipeline.SetPixelShader(&pix_shader);

	// register constant buffers
	shader_pipeline.RegisterConstBuffer("cbMeshInstance", g_pMeshInstanceCB);
	shader_pipeline.RegisterConstBuffer("cbGlobals", g_pGlobalsCB);

	
	// register samplers
	shader_pipeline.RegisterSampler("g_samWrap", GetDefaultSamplerWrap() );
	shader_pipeline.RegisterSampler("g_samClamp", GetDefaultSamplerClamp() );
	shader_pipeline.RegisterSampler("g_samShadow", GetDefaultShadowSampler() );

	// depth only pre-pass
	vert_shader_basic.CompileShaderFunction(pd3dDevice, L"shader_basic.hlsl", pDefines, "RenderSceneVS", "vs_5_0", dwShaderFlags );
	
	shader_dpthfill_pipeline.SetVertexShader(&vert_shader_basic);
	shader_dpthfill_pipeline.RegisterConstBuffer("cbMeshInstance", g_pMeshInstanceCB);
	shader_dpthfill_pipeline.RegisterConstBuffer("cbGlobals", g_pGlobalsCB);
#endif

	{
		debug_vol_vert_shader.CompileShaderFunction(pd3dDevice, L"shader_debug_volumes.hlsl", pDefines, "RenderDebugVolumeVS", "vs_5_0", dwShaderFlags );
		debug_vol_pix_shader.CompileShaderFunction(pd3dDevice, L"shader_debug_volumes.hlsl", pDefines, "YellowPS", "ps_5_0", dwShaderFlags );

		// prepare shader pipeline
		debug_volume_shader_pipeline.SetVertexShader(&debug_vol_vert_shader);
		debug_volume_shader_pipeline.SetPixelShader(&debug_vol_pix_shader);

		// register constant buffers
		debug_volume_shader_pipeline.RegisterConstBuffer("cbGlobals", g_pGlobalsCB);

	
		// register samplers
		debug_volume_shader_pipeline.RegisterSampler("g_samWrap", GetDefaultSamplerWrap() );
		debug_volume_shader_pipeline.RegisterSampler("g_samClamp", GetDefaultSamplerClamp() );
		debug_volume_shader_pipeline.RegisterSampler("g_samShadow", GetDefaultShadowSampler() );
	}


#ifndef SHOW_DEMO_SCENE
	// create all textures
	WCHAR dest_str[256];
	for(int t=0; t<NR_TEXTURES; t++)
	{
		wcscpy(dest_str, MODEL_PATH_W);
		wcscat(dest_str, tex_names[t]);

		V_RETURN(DXUTCreateShaderResourceViewFromFile(pd3dDevice, dest_str, &g_pTexturesHandler[t]));


		shader_pipeline.RegisterResourceView(stex_names[t], g_pTexturesHandler[t]);
		if(t==1) shader_dpthfill_pipeline.RegisterResourceView(stex_names[t], g_pTexturesHandler[t]);
	}
#endif
	

	

#ifndef SHOW_DEMO_SCENE
	g_cMesh.ReadMeshFil(pd3dDevice, MODEL_PATH  MODEL_NAME, 4000.0f, true, true);
#endif
	


	bd.ByteWidth = (sizeof( cbBoundsInfo )+0xf)&(~0xf);
	V_RETURN( pd3dDevice->CreateBuffer( &bd, NULL, &g_pVolumeClipInfo ) );


	D3D11_SHADER_RESOURCE_VIEW_DESC srvbuffer_desc;

	// attribute data for volumes such as attenuation, color, etc.
	g_VolumeDataBuffer.CreateBuffer(pd3dDevice, sizeof( SFiniteVolumeData ) * MAX_NR_VOLUMES_PER_CAMERA, sizeof( SFiniteVolumeData ), NULL, CBufferObject::StructuredBuf, true, false, CBufferObject::StagingCpuWriteOnly);
	g_VolumeDataBuffer.AddStructuredSRV(pd3dDevice);
#ifndef SHOW_DEMO_SCENE
	shader_pipeline.RegisterResourceView("g_vVolumeData", g_VolumeDataBuffer.GetSRV());
#endif
	debug_volume_shader_pipeline.RegisterResourceView("g_vVolumeData", g_VolumeDataBuffer.GetSRV());


	// buffer for GPU generated screen-space AABB per volume
	g_ScrSpaceAABounds.CreateBuffer(pd3dDevice, 2 * sizeof(Vec3) * MAX_NR_VOLUMES_PER_CAMERA, sizeof(Vec3), NULL, CBufferObject::StructuredBuf, true, true, CBufferObject::StagingCpuReadOnly);
	g_ScrSpaceAABounds.AddStructuredSRV(pd3dDevice);
	g_ScrSpaceAABounds.AddStructuredUAV(pd3dDevice);


	// a nonuniformly scaled OBB per volume
	g_OrientedBounds.CreateBuffer(pd3dDevice, sizeof(SFiniteVolumeBound) * MAX_NR_VOLUMES_PER_CAMERA, sizeof(SFiniteVolumeBound), NULL, CBufferObject::StructuredBuf, true, false, CBufferObject::StagingCpuWriteOnly);
	g_OrientedBounds.AddStructuredSRV(pd3dDevice);



	InitializeSceneGraph(pd3dDevice, pd3dImmediateContext, g_pGlobalsCB, g_VolumeDataBuffer.GetSRV());

	BuildVolumesBuffer();


	// create vertex decleration
#ifndef SHOW_DEMO_SCENE
	const D3D11_INPUT_ELEMENT_DESC vertexlayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,	0, ATTR_OFFS(SFilVert, pos),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,		0, ATTR_OFFS(SFilVert, s), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,		0, ATTR_OFFS(SFilVert, s2), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,	0, ATTR_OFFS(SFilVert, norm), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,0, ATTR_OFFS(SFilVert, tang), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    V_RETURN( pd3dDevice->CreateInputLayout( vertexlayout, ARRAYSIZE( vertexlayout ), 
                                             vert_shader.GetBufferPointer(), vert_shader.GetBufferSize(), 
                                             &g_pVertexLayout ) );

	const D3D11_INPUT_ELEMENT_DESC simplevertexlayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,	0, ATTR_OFFS(SFilVert, pos),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

	V_RETURN( pd3dDevice->CreateInputLayout( simplevertexlayout, ARRAYSIZE( simplevertexlayout ), 
                                             vert_shader_basic.GetBufferPointer(), vert_shader_basic.GetBufferSize(), 
                                             &g_pVertexSimpleLayout ) );
#endif

#ifdef SHOW_DEMO_SCENE
	g_shadowMap.InitShadowMap(pd3dDevice, g_pGlobalsCB, 4096, 4096);
	g_canvas.InitCanvas(pd3dDevice, g_pGlobalsCB);
#endif


	return S_OK;
}

//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11CreateDevice 
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11DestroyDevice( void* pUserContext )
{
	g_DialogResourceManager.OnD3D11DestroyDevice();
	SAFE_DELETE( g_pTxtHelper );

	g_tex_depth.CleanUp();
	
	g_VolumeDataBuffer.CleanUp();
	g_ScrSpaceAABounds.CleanUp();
	g_OrientedBounds.CleanUp();
	g_volumeListBuffer.CleanUp();
	
#ifndef SHOW_DEMO_SCENE
	SAFE_RELEASE( g_pMeshInstanceCB );
#endif
	SAFE_RELEASE( g_pGlobalsCB );
	SAFE_RELEASE( g_pVolumeClipInfo );

#ifndef SHOW_DEMO_SCENE
	for(int t=0; t<NR_TEXTURES; t++)
		SAFE_RELEASE( g_pTexturesHandler[t] );
#endif

	ReleaseSceneGraph();

#ifndef SHOW_DEMO_SCENE
	g_cMesh.CleanUp();
#endif

#ifndef SHOW_DEMO_SCENE
	vert_shader.CleanUp();
	pix_shader.CleanUp();
	vert_shader_basic.CleanUp();
#endif
	debug_vol_vert_shader.CleanUp();
	debug_vol_pix_shader.CleanUp();

	
	scrbound_shader.CleanUp();
	volumelist_coarse_shader.CleanUp();
	volumelist_exact_shader.CleanUp();

#ifndef SHOW_DEMO_SCENE
	SAFE_RELEASE( g_pVertexLayout );
	SAFE_RELEASE( g_pVertexSimpleLayout );
#endif

#ifdef SHOW_DEMO_SCENE
	g_shadowMap.CleanUp();
	g_canvas.CleanUp();
#endif

	DeinitUtils();
}


// [0;1] but right hand coordinate system
void myFrustum(float * pMat, const float fLeft, const float fRight, const float fBottom, const float fTop, const float fNear, const float fFar)
{
	// first column
	pMat[0*4 + 0] = (2 * fNear) / (fRight - fLeft); pMat[0*4 + 1] = 0; pMat[0*4 + 2] = 0; pMat[0*4 + 3] = 0;

	// second column
	pMat[1*4 + 0] = 0; pMat[1*4 + 1] = (2 * fNear) / (fTop - fBottom); pMat[1*4 + 2] = 0; pMat[1*4 + 3] = 0;

	// fourth column
	pMat[3*4 + 0] = 0; pMat[3*4 + 1] = 0; pMat[3*4 + 2] = -(fFar * fNear) / (fFar - fNear); pMat[3*4 + 3] = 0;

	// third column
	pMat[2*4 + 0] = (fRight + fLeft) / (fRight - fLeft);
	pMat[2*4 + 1] = (fTop + fBottom) / (fTop - fBottom);
	pMat[2*4 + 2] = -fFar / (fFar - fNear);
	pMat[2*4 + 3] = -1;

#ifdef LEFT_HAND_COORDINATES
	for(int r=0; r<4; r++) pMat[2*4 + r] = -pMat[2*4 + r];
#endif
}