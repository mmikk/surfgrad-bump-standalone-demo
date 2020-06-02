#include "scenegraph.h"
#include "meshimport/meshdraw.h"

#include "shader.h"
#include "shaderpipeline.h"
#include "shaderutils.h"
#include "std_cbuffer.h"
#include "custom_cbuffers.h"
#include "buffer.h"


#include <d3d11_2.h>

#include "DXUT.h"
#include "SDKmisc.h"


#ifndef M_PI
	#define M_PI	3.1415926535897932384626433832795f
#endif

static Vec3 g_vSunDir;


enum TEX_ID
{
	PIRATE_ALBEDO=0,
	PIRATE_DETAILS,
	PIRATE_MASK,
	PIRATE_OCCLUSION,
	PIRATE_SMOOTHNESS,
	PIRATE_NORMALS_TS,
	PIRATE_NORMALS_OS,

	ROCK_BASE_TS,
	ROCK_MOSSY,
	ROCK_CLIFF_MOSSY,
	ROCK_JAGGED,

	BASIC_HEIGHT,
	BASIC_NORMALS,
	BASIC_HARDSURFACE_NORMALS,
	POM_HEIGHTS,
	POM_NORMALS,
	BASIC_COBBLESTONE_NORMALS,
	BASIC_FACE_1_NORMALS,
	BASIC_FACE_2_NORMALS,
	BASIC_MASK,
	BASIC_SOFA_NORMALS,
	BASIC_SOFA_NORMALS_2X,
	BASIC_NORMALS_OS,

	DECAL_CUBEMAP_OS,
	TABLE_FG,

	NUM_TEXTURES
};

static ID3D11ShaderResourceView * g_pTexturesHandler[NUM_TEXTURES];

enum MESH_ID
{
	MESH_SPHERE=0,
	MESH_QUAD,
	MESH_HARDSURFACE,
	MESH_PIRATE,
	MESH_ROCK,
	MESH_LABELS,

	NUM_MESHES
};

static CMeshDraw g_pMeshes[NUM_MESHES];

#define MODEL_PATH		"meshes/"



enum DRAWABLE_ID
{
	GROUND_PLANE=0,

	BASIC_SAMPLE,
	NMAP_OS,
	SCALE_DEPENDENT,
	MIXING_SAMPLE,

	PLANAR_Y,
	TRIPLANAR_WS,
	TRIPLANAR_OS,

	HARDSURFACE,
	BUMP_FROM_FRACTALSUM_NOISE_3D,
	BUMP_FROM_TURBULENCE_NOISE_3D,
	BUMP_FROM_HEIGHTMAP,
	SHOW_TS_FROM_HEIGHTMAP,

	PARALLAX_DENTS,
	PARALLAX_DETAILS,
	PARALLAX_BASIC,

	TRIPLANAR_POST,

	PIRATE_EXAMPLE,
	
	NUM_DRAWABLES
};

enum
{
	DECALS_ENABLED_MIPMAPPED_ON=0,
	DECALS_ENABLED_MIPMAPPED_OFF,
	DECALS_DISABLED,

	NUM_PS_VARIANTS
};


static int g_decalMode = DECALS_ENABLED_MIPMAPPED_ON; 


static DWORD g_dwShaderFlags;

static CShader g_vert_shader;
static CShader g_vert_shader_basic;
static CShader g_vert_shader_basic_labels;
static CShader g_pix_shader_basic_white;
static CShader g_pix_shader[NUM_PS_VARIANTS*NUM_DRAWABLES];

static CShaderPipeline g_ShaderPipelines_DepthOnly[NUM_DRAWABLES];
static CShaderPipeline g_ShaderPipelines[NUM_PS_VARIANTS*NUM_DRAWABLES];

static ID3D11Buffer * g_pMaterialParamsCB[NUM_DRAWABLES];
static ID3D11Buffer * g_pMeshInstanceCB[NUM_DRAWABLES];
static ID3D11Buffer * g_pMeshInstanceCB_forLabels;
static Mat44 g_mLocToWorld[NUM_DRAWABLES];
static int g_meshResourceID[NUM_DRAWABLES];

// labels are special
static CShaderPipeline g_LabelsShaderPipelines;


static ID3D11InputLayout * g_pVertexLayout = NULL;
static ID3D11InputLayout * g_pVertexSimpleLayout = NULL;


#define str(x) #x

struct SValueAndStr
{
	SValueAndStr(const int value_in, const char str_name_in[]) : value(value_in), str_name(str_name_in) {}

	const int value;
	const char * str_name;
};


#define MAKE_STR_PAIR(x)	SValueAndStr(x, str(x) )
#define MAKE_STR_SIZE_PAIR(x) SValueAndStr(sizeof(x), str(x) )

static bool GenericPipelineSetup(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB, const Mat44 &mat, const SValueAndStr &sIdxAndStr, const char pixShaderEntryFunc[], const SValueAndStr sMatSizeAndStr);
static bool GenericPipelineSetup(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB, const Mat44 &mat, const SValueAndStr &sIdxAndStr, const char pixShaderEntryFunc[])
{
	SValueAndStr defaultMatSizeAndName(0,"");
	return GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, sIdxAndStr, pixShaderEntryFunc, defaultMatSizeAndName);
}


const float g_O = 2*2.59f;
const float g_S = -1.0f;		// convert unity scene to right hand frame.

static void SetScaleAndPos(Mat44 * matPtr, const float scale, const Vec3 &pos)
{
	LoadIdentity(matPtr);
	for(int c=0; c<3; c++)
	{
		SetColumn(matPtr, c, Vec4(c==0 ? scale : 0.0f, c==1 ? scale : 0.0f, c==2 ? scale : 0.0f, 0.0f));
	}
	SetColumn(matPtr, 3, Vec4(pos.x, pos.y, pos.z, 1.0f));
}

static void SetupGroundPlanePipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 6.0f, Vec3(g_S*3.39f+g_O, 1.28f, -0.003f) );
	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(GROUND_PLANE), "SuperSimplePS");

	

	g_meshResourceID[GROUND_PLANE] = MESH_QUAD;
}


static void SetupBasicSamplePipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.5f, Vec3(g_S*4.64f+g_O, 1.829f, -3.114573f) );
	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(BASIC_SAMPLE), "BasicSamplePS", MAKE_STR_SIZE_PAIR(cbMatBasicShader));

	for(int i=0; i<NUM_PS_VARIANTS; i++)
		g_ShaderPipelines[i*NUM_DRAWABLES+BASIC_SAMPLE].RegisterResourceView("g_norm_tex", g_pTexturesHandler[BASIC_NORMALS]);

	


	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[BASIC_SAMPLE], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatBasicShader *)MappedSubResource.pData)->g_fTileRate = 5.0f;
	((cbMatBasicShader *)MappedSubResource.pData)->g_fBumpIntensity = 1.3f;
	((cbMatBasicShader *)MappedSubResource.pData)->g_bUseVertexTSpace = false;
    pContext->Unmap( g_pMaterialParamsCB[BASIC_SAMPLE], 0 );
			  

	g_meshResourceID[BASIC_SAMPLE] = MESH_SPHERE;
}

static void SetupNMapOSPipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.5f, Vec3(g_S*4.64f+g_O, 1.829f, -1.933f) );
	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(NMAP_OS), "NMapOSSamplePS", MAKE_STR_SIZE_PAIR(cbMatNMapOSShader));

	for(int i=0; i<NUM_PS_VARIANTS; i++)
		g_ShaderPipelines[i*NUM_DRAWABLES+NMAP_OS].RegisterResourceView("g_norm_os_tex", g_pTexturesHandler[BASIC_NORMALS_OS]);

	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[NMAP_OS], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatNMapOSShader *)MappedSubResource.pData)->g_fBumpIntensity = 1.3f;
    pContext->Unmap( g_pMaterialParamsCB[NMAP_OS], 0 );
	

	g_meshResourceID[NMAP_OS] = MESH_SPHERE;
}

static void SetupScaleDependentPipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.5f, Vec3(g_S*4.64f+g_O, 1.829f, -0.7540001f) );
	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(SCALE_DEPENDENT), "ScaleDependentSamplePS", MAKE_STR_SIZE_PAIR(cbMatScaleDependentShader));

	for(int i=0; i<NUM_PS_VARIANTS; i++)
		g_ShaderPipelines[i*NUM_DRAWABLES+SCALE_DEPENDENT].RegisterResourceView("g_norm_tex", g_pTexturesHandler[BASIC_NORMALS]);

	
	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[SCALE_DEPENDENT], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatScaleDependentShader *)MappedSubResource.pData)->g_fBumpIntensity = 0.002f;
	((cbMatScaleDependentShader *)MappedSubResource.pData)->g_fTileRate = 5.0f;
    pContext->Unmap( g_pMaterialParamsCB[SCALE_DEPENDENT], 0 );

	g_meshResourceID[SCALE_DEPENDENT] = MESH_SPHERE;
}

static void SetupMixingSamplePipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.5f, Vec3(g_S*7.320175f+g_O, 1.856f, -0.4705267f) );
	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(MIXING_SAMPLE), "MixingSamplePS", MAKE_STR_SIZE_PAIR(cbMatMixingShader));

	for(int i=0; i<NUM_PS_VARIANTS; i++)
	{
		g_ShaderPipelines[i*NUM_DRAWABLES+MIXING_SAMPLE].RegisterResourceView("g_norm_os_tex", g_pTexturesHandler[BASIC_NORMALS_OS]);
		g_ShaderPipelines[i*NUM_DRAWABLES+MIXING_SAMPLE].RegisterResourceView("g_norm_detail_tex", g_pTexturesHandler[BASIC_SOFA_NORMALS_2X]);
		g_ShaderPipelines[i*NUM_DRAWABLES+MIXING_SAMPLE].RegisterResourceView("g_mask_tex", g_pTexturesHandler[BASIC_MASK]);
	}

	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[MIXING_SAMPLE], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatMixingShader *)MappedSubResource.pData)->g_fBaseBumpScale = 1.0f;
	((cbMatMixingShader *)MappedSubResource.pData)->g_fDetailBumpScale = 2.5f;
	((cbMatMixingShader *)MappedSubResource.pData)->g_fDetailTileRate = 8.0f;
	((cbMatMixingShader *)MappedSubResource.pData)->g_bUseSecondaryUVsForDetails = false;
	((cbMatMixingShader *)MappedSubResource.pData)->g_fNoiseTileRate = 0.3f;
	((cbMatMixingShader *)MappedSubResource.pData)->g_fNoiseBumpScale = -0.8*0.27f;
	((cbMatMixingShader *)MappedSubResource.pData)->g_bInvertBumpping = false;
    pContext->Unmap( g_pMaterialParamsCB[MIXING_SAMPLE], 0 );


	g_meshResourceID[MIXING_SAMPLE] = MESH_SPHERE;
}

static void SetupPlanarYPipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.5f, Vec3(g_S*4.64f+g_O, 1.829f, 0.5159999f) );
	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(PLANAR_Y), "PlanarYSamplePS", MAKE_STR_SIZE_PAIR(cbMatPlanarY));

	for(int i=0; i<NUM_PS_VARIANTS; i++)
		g_ShaderPipelines[i*NUM_DRAWABLES+PLANAR_Y].RegisterResourceView("g_norm_tex", g_pTexturesHandler[BASIC_NORMALS]);

	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[PLANAR_Y], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatPlanarY *)MappedSubResource.pData)->g_fBumpIntensity = 2.0f;
	((cbMatPlanarY *)MappedSubResource.pData)->g_fTileRate = 2.0f;
    pContext->Unmap( g_pMaterialParamsCB[PLANAR_Y], 0 );
			  

	g_meshResourceID[PLANAR_Y] = MESH_SPHERE;
}

static void SetupTriplanarWSPipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.5f, Vec3(g_S*4.64f+g_O, 1.829f, 1.831f) );
	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(TRIPLANAR_WS), "TriplanarWS_PS", MAKE_STR_SIZE_PAIR(cbMatTriplanar));

	for(int i=0; i<NUM_PS_VARIANTS; i++)
	{
		g_ShaderPipelines[i*NUM_DRAWABLES+TRIPLANAR_WS].RegisterResourceView("g_nmapX", g_pTexturesHandler[BASIC_FACE_1_NORMALS]);
		g_ShaderPipelines[i*NUM_DRAWABLES+TRIPLANAR_WS].RegisterResourceView("g_nmapY", g_pTexturesHandler[BASIC_NORMALS]);
		g_ShaderPipelines[i*NUM_DRAWABLES+TRIPLANAR_WS].RegisterResourceView("g_nmapZ", g_pTexturesHandler[BASIC_FACE_2_NORMALS]);
	}
	
	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[TRIPLANAR_WS], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatTriplanar *)MappedSubResource.pData)->g_fBumpIntensity = 1.5f;
	((cbMatTriplanar *)MappedSubResource.pData)->g_fTileRate = 2.0f;
    pContext->Unmap( g_pMaterialParamsCB[TRIPLANAR_WS], 0 );
			  

	g_meshResourceID[TRIPLANAR_WS] = MESH_SPHERE;
}

static void SetupTriplanarOSPipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.5f, Vec3(g_S*4.558f+g_O, 1.878f, 3.348f) );
	const float deg2rad = M_PI/180.0f;
	Mat44 rot; LoadIdentity(&rot);

	// not identical rotation to the unity scene but doesn't matter
	LoadRotation(&rot, 20.307f*deg2rad, -8.124001f*deg2rad, -22.358f*deg2rad);
	mat = mat * rot;

	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(TRIPLANAR_OS), "TriplanarOS_PS", MAKE_STR_SIZE_PAIR(cbMatTriplanar));

	for(int i=0; i<NUM_PS_VARIANTS; i++)
	{
		g_ShaderPipelines[i*NUM_DRAWABLES+TRIPLANAR_OS].RegisterResourceView("g_nmapX", g_pTexturesHandler[BASIC_FACE_1_NORMALS]);
		g_ShaderPipelines[i*NUM_DRAWABLES+TRIPLANAR_OS].RegisterResourceView("g_nmapY", g_pTexturesHandler[BASIC_NORMALS]);
		g_ShaderPipelines[i*NUM_DRAWABLES+TRIPLANAR_OS].RegisterResourceView("g_nmapZ", g_pTexturesHandler[BASIC_FACE_2_NORMALS]);
	}


	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[TRIPLANAR_OS], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatTriplanar *)MappedSubResource.pData)->g_fBumpIntensity = 1.5f;
	((cbMatTriplanar *)MappedSubResource.pData)->g_fTileRate = 1.0f;	//2.0f;
    pContext->Unmap( g_pMaterialParamsCB[TRIPLANAR_OS], 0 );
			  

	g_meshResourceID[TRIPLANAR_OS] = MESH_SPHERE;
}


static void SetupBumpFromHeightMapPipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.5f, Vec3(g_S*7.320175f+g_O, 1.856f, 0.8104733f) );
	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(BUMP_FROM_HEIGHTMAP), "BumpFromHeightMapPS", MAKE_STR_SIZE_PAIR(cbMatBumpFromHeightShader));

	for(int i=0; i<NUM_PS_VARIANTS; i++)
		g_ShaderPipelines[i*NUM_DRAWABLES+BUMP_FROM_HEIGHTMAP].RegisterResourceView("g_height_tex", g_pTexturesHandler[BASIC_HEIGHT]);

	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[BUMP_FROM_HEIGHTMAP], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatBumpFromHeightShader *)MappedSubResource.pData)->g_fTileRate = 5.0f;
	((cbMatBumpFromHeightShader *)MappedSubResource.pData)->g_fBumpIntensity = 7.0f;
	//((cbMatBumpFromHeightShader *)MappedSubResource.pData)->g_bUseUpscaleHQ = true;
    pContext->Unmap( g_pMaterialParamsCB[BUMP_FROM_HEIGHTMAP], 0 );
			  

	g_meshResourceID[BUMP_FROM_HEIGHTMAP] = MESH_SPHERE;
}

static void SetupShowTSFromHeightsPipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.5f, Vec3(g_S*2.181f+g_O, 1.878f, 3.348f) );
	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(SHOW_TS_FROM_HEIGHTMAP), "ShowTSFromHeightMapPS", MAKE_STR_SIZE_PAIR(cbMatShowFromHeightsTS));

	for(int i=0; i<NUM_PS_VARIANTS; i++)
	{
		g_ShaderPipelines[i*NUM_DRAWABLES+SHOW_TS_FROM_HEIGHTMAP].RegisterResourceView("g_norm_tex", g_pTexturesHandler[BASIC_NORMALS]);
		g_ShaderPipelines[i*NUM_DRAWABLES+SHOW_TS_FROM_HEIGHTMAP].RegisterResourceView("g_height_tex", g_pTexturesHandler[BASIC_HEIGHT]);
	}
	
	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[SHOW_TS_FROM_HEIGHTMAP], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatShowFromHeightsTS *)MappedSubResource.pData)->g_fTileRate = 5.0f;
	((cbMatShowFromHeightsTS *)MappedSubResource.pData)->g_fBumpIntensity = 5.5f;
	((cbMatShowFromHeightsTS *)MappedSubResource.pData)->g_iSamplingMethod = 0;			// 0 - upscaleHQ, 1 - 3 tap, 2 - from normal map
    pContext->Unmap( g_pMaterialParamsCB[SHOW_TS_FROM_HEIGHTMAP], 0 );
			  

	g_meshResourceID[SHOW_TS_FROM_HEIGHTMAP] = MESH_SPHERE;
}


static void SetupParallaxWithDentsPipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.5f, Vec3(0.0f, 0.0f, 0.0f) );
	Mat44 rotX; LoadIdentity(&rotX);
	LoadRotation(&rotX, M_PI/2.0f, 0.0f, 0.0f);
	Mat44 rotY; LoadIdentity(&rotY);
	LoadRotation(&rotY, 0.0f, -M_PI/2.0f, 0.0f);
	mat = rotY * rotX * mat;
	SetColumn(&mat, 3, Vec4(g_S*2.59f+g_O, 1.92f, -2.164f, 1.0f));

	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(PARALLAX_DENTS), "ParallaxDentsPS", MAKE_STR_SIZE_PAIR(cbMatParallaxDents));

	for(int i=0; i<NUM_PS_VARIANTS; i++)
	{
		g_ShaderPipelines[i*NUM_DRAWABLES+PARALLAX_DENTS].RegisterResourceView("g_height_tex", g_pTexturesHandler[POM_HEIGHTS]);
		g_ShaderPipelines[i*NUM_DRAWABLES+PARALLAX_DENTS].RegisterResourceView("g_norm_tex", g_pTexturesHandler[POM_NORMALS]);
	}
	
	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[PARALLAX_DENTS], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatParallaxDents *)MappedSubResource.pData)->g_fTileRate = 1.0f;
	((cbMatParallaxDents *)MappedSubResource.pData)->g_fBumpScale = 0.1f;
	((cbMatParallaxDents *)MappedSubResource.pData)->g_fNoiseTileRate = 13.0f;
	((cbMatParallaxDents *)MappedSubResource.pData)->g_fNoiseBumpScale = -0.006f;

	((cbMatParallaxDents *)MappedSubResource.pData)->g_vVolumeBumpOffset = Vec3(0.1f, 1.0f, 0.0f);
	((cbMatParallaxDents *)MappedSubResource.pData)->g_fVolumeBumpPowerValue = 5.0f;
    pContext->Unmap( g_pMaterialParamsCB[PARALLAX_DENTS], 0 );
	

	g_meshResourceID[PARALLAX_DENTS] = MESH_QUAD;

}


static void SetupParallaxWithDetailsPipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.5f, Vec3(0.0f, 0.0f, 0.0f) );
	Mat44 rotX; LoadIdentity(&rotX);
	LoadRotation(&rotX, M_PI/2.0f, 0.0f, 0.0f);
	Mat44 rotY; LoadIdentity(&rotY);
	LoadRotation(&rotY, 0.0f, -M_PI/2.0f, 0.0f);
	mat = rotY * rotX * mat;
	SetColumn(&mat, 3, Vec4(g_S*2.59f+g_O,1.92f, -0.865f, 1.0f));

	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(PARALLAX_DETAILS), "ParallaxDetailsPS", MAKE_STR_SIZE_PAIR(cbMatParallaxDetails));

	for(int i=0; i<NUM_PS_VARIANTS; i++)
	{
		g_ShaderPipelines[i*NUM_DRAWABLES+PARALLAX_DETAILS].RegisterResourceView("g_height_tex", g_pTexturesHandler[POM_HEIGHTS]);
		g_ShaderPipelines[i*NUM_DRAWABLES+PARALLAX_DETAILS].RegisterResourceView("g_norm_tex", g_pTexturesHandler[POM_NORMALS]);
		g_ShaderPipelines[i*NUM_DRAWABLES+PARALLAX_DETAILS].RegisterResourceView("g_norm_detail_tex", g_pTexturesHandler[BASIC_COBBLESTONE_NORMALS]);
	}

	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[PARALLAX_DETAILS], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatParallaxDetails *)MappedSubResource.pData)->g_fTileRate = 1.0f;
	((cbMatParallaxDetails *)MappedSubResource.pData)->g_fBumpScale = 0.1f;
	((cbMatParallaxDetails *)MappedSubResource.pData)->g_fDetailTileRate = 2.0f;
    pContext->Unmap( g_pMaterialParamsCB[PARALLAX_DETAILS], 0 );
	

	g_meshResourceID[PARALLAX_DETAILS] = MESH_QUAD;

}

static void SetupParallaxBasicPipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.5f, Vec3(0.0f, 0.0f, 0.0f) );
	Mat44 rotX; LoadIdentity(&rotX);
	LoadRotation(&rotX, M_PI/2.0f, 0.0f, 0.0f);
	Mat44 rotY; LoadIdentity(&rotY);
	LoadRotation(&rotY, 0.0f, -M_PI/2.0f, 0.0f);
	mat = rotY * rotX * mat;
	SetColumn(&mat, 3, Vec4(g_S*2.59f+g_O,1.92f, 0.415f, 1.0f));

	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(PARALLAX_BASIC), "ParallaxBasicPS", MAKE_STR_SIZE_PAIR(cbMatParallaxBasic));

	for(int i=0; i<NUM_PS_VARIANTS; i++)
	{
		g_ShaderPipelines[i*NUM_DRAWABLES+PARALLAX_BASIC].RegisterResourceView("g_height_tex", g_pTexturesHandler[POM_HEIGHTS]);
		g_ShaderPipelines[i*NUM_DRAWABLES+PARALLAX_BASIC].RegisterResourceView("g_norm_tex", g_pTexturesHandler[POM_NORMALS]);
	}

	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[PARALLAX_BASIC], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatParallaxBasic *)MappedSubResource.pData)->g_fTileRate = 1.0f;
	((cbMatParallaxBasic *)MappedSubResource.pData)->g_fBumpScale = 0.1f;
    pContext->Unmap( g_pMaterialParamsCB[PARALLAX_BASIC], 0 );

	
	g_meshResourceID[PARALLAX_BASIC] = MESH_QUAD;

}

static void SetupFractalsumNoisePipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.5f, Vec3(g_S*7.320175f+g_O, 1.856f, -2.874527f) );
	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(BUMP_FROM_FRACTALSUM_NOISE_3D), "BumpFromFractalsumPS", MAKE_STR_SIZE_PAIR(cbMatNoise));

	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[BUMP_FROM_FRACTALSUM_NOISE_3D], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatNoise *)MappedSubResource.pData)->g_fNoiseBumpScale = 0.13f;
	((cbMatNoise *)MappedSubResource.pData)->g_fNoiseTileRate = 0.73f;
    pContext->Unmap( g_pMaterialParamsCB[BUMP_FROM_FRACTALSUM_NOISE_3D], 0 );
			  

	g_meshResourceID[BUMP_FROM_FRACTALSUM_NOISE_3D] = MESH_SPHERE;
}

static void SetupTurbulenceNoisePipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.5f, Vec3(g_S*7.320175f+g_O, 1.856f, -1.633527f) );
	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(BUMP_FROM_TURBULENCE_NOISE_3D), "BumpFromTurbulencePS", MAKE_STR_SIZE_PAIR(cbMatNoise));

	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[BUMP_FROM_TURBULENCE_NOISE_3D], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatNoise *)MappedSubResource.pData)->g_fNoiseBumpScale = 0.18;
	((cbMatNoise *)MappedSubResource.pData)->g_fNoiseTileRate = 0.73;
    pContext->Unmap( g_pMaterialParamsCB[BUMP_FROM_TURBULENCE_NOISE_3D], 0 );
			  

	g_meshResourceID[BUMP_FROM_TURBULENCE_NOISE_3D] = MESH_SPHERE;
}

static void SetupPirateExamplePipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 1.0f, Vec3(0.0f, 0.0f, 0.0f) );
	Mat44 rotY; LoadIdentity(&rotY);
	LoadRotation(&rotY, 0.0f, -M_PI/2.0f, 0.0f);
	mat = rotY * mat;
	SetColumn(&mat, 3, Vec4(g_S*-0.007558346f+g_O,2.237f, -2.74f, 1.0f));

	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(PIRATE_EXAMPLE), "PirateExamplePS", MAKE_STR_SIZE_PAIR(cbMatPirateShader));

	for(int i=0; i<NUM_PS_VARIANTS; i++)
	{
		g_ShaderPipelines[i*NUM_DRAWABLES+PIRATE_EXAMPLE].RegisterResourceView("g_norm_tex", g_pTexturesHandler[PIRATE_NORMALS_TS]);
		g_ShaderPipelines[i*NUM_DRAWABLES+PIRATE_EXAMPLE].RegisterResourceView("g_albedo_tex", g_pTexturesHandler[PIRATE_ALBEDO]);
		g_ShaderPipelines[i*NUM_DRAWABLES+PIRATE_EXAMPLE].RegisterResourceView("g_smoothness_tex", g_pTexturesHandler[PIRATE_SMOOTHNESS]);
		g_ShaderPipelines[i*NUM_DRAWABLES+PIRATE_EXAMPLE].RegisterResourceView("g_ao_tex", g_pTexturesHandler[PIRATE_OCCLUSION]);

		g_ShaderPipelines[i*NUM_DRAWABLES+PIRATE_EXAMPLE].RegisterResourceView("g_norm_os_tex", g_pTexturesHandler[PIRATE_NORMALS_OS]);
		g_ShaderPipelines[i*NUM_DRAWABLES+PIRATE_EXAMPLE].RegisterResourceView("g_norm_detail_tex", g_pTexturesHandler[PIRATE_DETAILS]);
		g_ShaderPipelines[i*NUM_DRAWABLES+PIRATE_EXAMPLE].RegisterResourceView("g_mask_tex", g_pTexturesHandler[PIRATE_MASK]);
	}

	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[PIRATE_EXAMPLE], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatPirateShader *)MappedSubResource.pData)->g_fDetailBumpScale = 1.7f;
	((cbMatPirateShader *)MappedSubResource.pData)->g_fDetailTileRate = 7.0f;
	//((cbMatPirateShader *)MappedSubResource.pData)->g_bUseSecondaryUVForDetailMap = true;
	((cbMatPirateShader *)MappedSubResource.pData)->g_fHairNoiseBumpScale = 0.001;
	((cbMatPirateShader *)MappedSubResource.pData)->g_fHairNoiseTileRate = 1.7;
	((cbMatPirateShader *)MappedSubResource.pData)->g_bHairNoisePostResolve = true;
	((cbMatPirateShader *)MappedSubResource.pData)->g_iTSpaceMode = 0;					 // 0 - vertex tspace,  1 - on the fly tspace, 2 - object space nmap
    pContext->Unmap( g_pMaterialParamsCB[PIRATE_EXAMPLE], 0 );


	g_meshResourceID[PIRATE_EXAMPLE] = MESH_PIRATE;
}

static void SetupHardSurfacePipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.7f, Vec3(g_S*1.315295f+g_O, 1.28f, 1.12f) );
	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(HARDSURFACE), "HardSurfaceSamplePS");

	for(int i=0; i<NUM_PS_VARIANTS; i++)
	{
		g_ShaderPipelines[i*NUM_DRAWABLES+HARDSURFACE].RegisterResourceView("g_norm_tex", g_pTexturesHandler[BASIC_HARDSURFACE_NORMALS]);
	}

	g_meshResourceID[HARDSURFACE] = MESH_HARDSURFACE;
}

static void SetupTriplanarPostResolvePipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 0.01*0.15f, Vec3(g_S*3.957f+g_O - 3.3, 0.119f + 2, 3.133f + 0.1f) );
	const float deg2rad = M_PI/180.0f;
	Mat44 rot; LoadIdentity(&rot);

	// not identical rotation to the unity scene but doesn't matter
	LoadRotation(&rot, 0.0f*deg2rad, -90.0f*deg2rad, 0.0f*deg2rad);
	mat = mat * rot;


	GenericPipelineSetup(pd3dDevice, pContext, pGlobalsCB, mat, MAKE_STR_PAIR(TRIPLANAR_POST), "TriplanarPostPS", MAKE_STR_SIZE_PAIR(cbMatTriplanarPost));

	for(int i=0; i<NUM_PS_VARIANTS; i++)
	{
		g_ShaderPipelines[i*NUM_DRAWABLES+TRIPLANAR_POST].RegisterResourceView("g_norm_tex", g_pTexturesHandler[ROCK_BASE_TS]);

		g_ShaderPipelines[i*NUM_DRAWABLES+TRIPLANAR_POST].RegisterResourceView("g_nmapX", g_pTexturesHandler[ROCK_JAGGED]);
		g_ShaderPipelines[i*NUM_DRAWABLES+TRIPLANAR_POST].RegisterResourceView("g_nmapY", g_pTexturesHandler[ROCK_MOSSY]);
		g_ShaderPipelines[i*NUM_DRAWABLES+TRIPLANAR_POST].RegisterResourceView("g_nmapZ", g_pTexturesHandler[ROCK_CLIFF_MOSSY]);
	}

	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMaterialParamsCB[TRIPLANAR_POST], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMatTriplanarPost *)MappedSubResource.pData)->g_fBaseBumpScale = 1.0f;
	((cbMatTriplanarPost *)MappedSubResource.pData)->g_fBaseTileRate = 1.0f;
	((cbMatTriplanarPost *)MappedSubResource.pData)->g_fBumpIntensity = 1.4f;
	((cbMatTriplanarPost *)MappedSubResource.pData)->g_fTileRate = 2.3f;
    pContext->Unmap( g_pMaterialParamsCB[TRIPLANAR_POST], 0 );


	g_meshResourceID[TRIPLANAR_POST] = MESH_ROCK;
}

static void SetupTextLabelsPipeline(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB)
{
	Mat44 mat; SetScaleAndPos(&mat, 10.0f, Vec3(g_S*3.39f+g_O - 1.37, 1.28f, -0.003f + 1.0-0.55) );
	const float deg2rad = M_PI/180.0f;
	Mat44 rot; LoadIdentity(&rot);
	LoadRotation(&rot, 0.0f*deg2rad, -90.0f*deg2rad, 0.0f*deg2rad);
	mat = mat * rot;



	CShaderPipeline &pipe = g_LabelsShaderPipelines;
	
	// prepare shader pipeline
	pipe.SetVertexShader(&g_vert_shader_basic_labels);
	pipe.SetPixelShader(&g_pix_shader_basic_white);

	// register constant buffers
	pipe.RegisterConstBuffer("cbMeshInstance", g_pMeshInstanceCB_forLabels);
	pipe.RegisterConstBuffer("cbGlobals", pGlobalsCB);
	//RegisterGenericNoiseBuffers(pipe);
	
	// register samplers
	//pipe.RegisterSampler("g_samWrap", GetDefaultSamplerWrap() );
	//pipe.RegisterSampler("g_samClamp", GetDefaultSamplerClamp() );
	//pipe.RegisterSampler("g_samShadow", GetDefaultShadowSampler() );


	// update transformation cb
	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMeshInstanceCB_forLabels, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMeshInstance *)MappedSubResource.pData)->g_mLocToWorld = Transpose(mat); 
	((cbMeshInstance *)MappedSubResource.pData)->g_mWorldToLocal = Transpose(~mat); 
    pContext->Unmap( g_pMeshInstanceCB_forLabels, 0 );


	//g_meshResourceID[BUMP_FROM_TURBULENCE_NOISE_3D] = MESH_SPHERE;
}




bool ImportTexture(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, const int resourceIdx, const WCHAR path[], const WCHAR name[], const bool sRGB=false)
{
	
	WCHAR dest_str[256];
	wcscpy(dest_str, path);
	wcscat(dest_str, name);

	HRESULT hr;

	CDXUTResourceCache &cache = DXUTGetGlobalResourceCache();
	V_RETURN( cache.CreateTextureFromFile(pd3dDevice, pContext, dest_str, &g_pTexturesHandler[resourceIdx], sRGB) );

	//V_RETURN(DXUTCreateShaderResourceViewFromFile(pd3dDevice, dest_str, &g_pTexturesHandler[resourceIdx]));

	int iTing;
	iTing = 0;
}

static bool CreateNoiseData(ID3D11Device* pd3dDevice);

bool InitResources(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB, ID3D11ShaderResourceView * pVolumeDataStructBuf)
{
	// import raw textures
	ImportTexture(pd3dDevice, pContext, PIRATE_ALBEDO, L"textures/Pirate/", L"Pirate_Albedo.png", true);
	ImportTexture(pd3dDevice, pContext, PIRATE_DETAILS, L"textures/Pirate/", L"Pirate_details.png");
	ImportTexture(pd3dDevice, pContext, PIRATE_MASK, L"textures/Pirate/", L"Pirate_Mask_rgb.png");
	ImportTexture(pd3dDevice, pContext, PIRATE_OCCLUSION, L"textures/Pirate/", L"Pirate_occlusion.png");
	ImportTexture(pd3dDevice, pContext, PIRATE_SMOOTHNESS, L"textures/Pirate/", L"Pirate_Smoothness.png");
	ImportTexture(pd3dDevice, pContext, PIRATE_NORMALS_TS, L"textures/Pirate/", L"Pirate_ts_normals.png");
	ImportTexture(pd3dDevice, pContext, PIRATE_NORMALS_OS, L"textures/Pirate/", L"Pirate_ws_normals.png");
	
	ImportTexture(pd3dDevice, pContext, ROCK_BASE_TS, L"textures/Rock/", L"Rock_Overgrown_A_Normal.tif");
	ImportTexture(pd3dDevice, pContext, ROCK_MOSSY, L"textures/Rock/", L"Moss_A_Normal.tif");
	ImportTexture(pd3dDevice, pContext, ROCK_CLIFF_MOSSY, L"textures/Rock/", L"Cliff_Mossy_B_Normal.tif");
	ImportTexture(pd3dDevice, pContext, ROCK_JAGGED, L"textures/Rock/", L"Rock_Jagged_B_Normal.tif");
	

	ImportTexture(pd3dDevice, pContext, BASIC_HEIGHT, L"textures/tileable/", L"base_height.tiff");
	ImportTexture(pd3dDevice, pContext, BASIC_NORMALS, L"textures/tileable/", L"base_height_conv_to_nmap.png");
	ImportTexture(pd3dDevice, pContext, BASIC_HARDSURFACE_NORMALS, L"textures/tileable/", L"Baked_normal.tiff");
	ImportTexture(pd3dDevice, pContext, POM_HEIGHTS, L"textures/tileable/", L"Dump.tif");
	ImportTexture(pd3dDevice, pContext, POM_NORMALS, L"textures/tileable/", L"Cone_Map_1k_normals.tiff");
	ImportTexture(pd3dDevice, pContext, BASIC_COBBLESTONE_NORMALS, L"textures/tileable/", L"details.png");
	ImportTexture(pd3dDevice, pContext, BASIC_FACE_1_NORMALS, L"textures/tileable/", L"face.jpg");
	ImportTexture(pd3dDevice, pContext, BASIC_FACE_2_NORMALS, L"textures/tileable/", L"face_2.png");
	ImportTexture(pd3dDevice, pContext, BASIC_MASK, L"textures/tileable/", L"mask.png");
	ImportTexture(pd3dDevice, pContext, BASIC_SOFA_NORMALS, L"textures/tileable/", L"sofa.png");
	ImportTexture(pd3dDevice, pContext, BASIC_SOFA_NORMALS_2X, L"textures/tileable/", L"sofa_dup.png");
	ImportTexture(pd3dDevice, pContext, BASIC_NORMALS_OS, L"textures/tileable/", L"sphere_os_normal.png");

	//ImportTexture(pd3dDevice, pContext, DECAL_CUBEMAP_OS, L"textures/tileable/", L"os_cube_nm_legacy_cube_radiance.dds");
	//ImportTexture(pd3dDevice, pContext, DECAL_CUBEMAP_OS, L"textures/tileable/", L"os_cube_nm_cube_radiance.dds");
	ImportTexture(pd3dDevice, pContext, DECAL_CUBEMAP_OS, L"textures/tileable/", L"andys_cobbles_legacy.dds");

	ImportTexture(pd3dDevice, pContext, TABLE_FG, L"textures/sky/", L"tableFG_B.dds");
	


	// import raw mesh data
	bool res = true;
	res &= g_pMeshes[MESH_SPHERE].ReadObj(pd3dDevice, MODEL_PATH "sphere.obj", 1.0f, true);
	res &= g_pMeshes[MESH_QUAD].ReadObj(pd3dDevice, MODEL_PATH "quad.obj", 1.0f, true);
	res &= g_pMeshes[MESH_HARDSURFACE].ReadObj(pd3dDevice, MODEL_PATH "LP_Normal_Map_Stress_Test.obj", 1.0f, false);
	res &= g_pMeshes[MESH_PIRATE].ReadObj(pd3dDevice, MODEL_PATH "LP_Pirate.obj", 1.0f, false);
	res &= g_pMeshes[MESH_ROCK].ReadObj(pd3dDevice, MODEL_PATH "Rock_Overgrown_A.obj", 1.0f, true);
	res &= g_pMeshes[MESH_LABELS].ReadObj(pd3dDevice, MODEL_PATH "Text_Positioned.obj", 1.0f, false);
	
	


	DWORD g_dwShaderFlags = D3D10_SHADER_OPTIMIZATION_LEVEL1;//D3D10_SHADER_ENABLE_STRICTNESS;
#if defined( DEBUG ) || defined( _DEBUG )
    // Set the D3D10_SHADER_DEBUG flag to embed debug information in the shaders.
    // Setting this flag improves the shader debugging experience, but still allows 
    // the shaders to be optimized and to run exactly the way they will run in 
    // the release configuration of this program.
    g_dwShaderFlags |= D3D10_SHADER_DEBUG;
#endif

	CONST D3D10_SHADER_MACRO* pDefines = NULL;
	g_vert_shader.CompileShaderFunction(pd3dDevice, L"shader_lighting.hlsl", pDefines, "RenderSceneVS", "vs_5_0", g_dwShaderFlags );

	// depth only pre-pass
	g_vert_shader_basic.CompileShaderFunction(pd3dDevice, L"shader_basic.hlsl", pDefines, "RenderSceneVS", "vs_5_0", g_dwShaderFlags );

	// labels shader
	g_vert_shader_basic_labels.CompileShaderFunction(pd3dDevice, L"shader_basic.hlsl", pDefines, "RenderSceneLabelsVS", "vs_5_0", g_dwShaderFlags );
	g_pix_shader_basic_white.CompileShaderFunction(pd3dDevice, L"shader_basic.hlsl", pDefines, "WhitePS", "ps_5_0", g_dwShaderFlags );

	// noise data
	CreateNoiseData(pd3dDevice);
	
	HRESULT hr;
	for(int i=0; i<NUM_DRAWABLES; i++)
	{
		g_pMaterialParamsCB[i]=NULL;
		g_pMeshInstanceCB[i]=NULL;
		g_meshResourceID[i]=-1;;
		LoadIdentity(&g_mLocToWorld[i]);

		// create constant buffers
		D3D11_BUFFER_DESC bd;

		memset(&bd, 0, sizeof(bd));
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.ByteWidth = (sizeof( cbMeshInstance )+0xf)&(~0xf);
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bd.MiscFlags = 0;
		V_RETURN( pd3dDevice->CreateBuffer( &bd, NULL, &g_pMeshInstanceCB[i] ) );
	}

	// create labels cb
	{
		g_pMeshInstanceCB_forLabels = NULL;
		
		// create constant buffers
		D3D11_BUFFER_DESC bd;

		memset(&bd, 0, sizeof(bd));
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.ByteWidth = (sizeof( cbMeshInstance )+0xf)&(~0xf);
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bd.MiscFlags = 0;
		V_RETURN( pd3dDevice->CreateBuffer( &bd, NULL, &g_pMeshInstanceCB_forLabels ) );
	}


	// create vertex decleration
	const D3D11_INPUT_ELEMENT_DESC vertexlayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,	0, ATTR_OFFS(SFilVert, pos),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,		0, ATTR_OFFS(SFilVert, s), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,		0, ATTR_OFFS(SFilVert, s2), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,	0, ATTR_OFFS(SFilVert, norm), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,0, ATTR_OFFS(SFilVert, tang), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    V_RETURN( pd3dDevice->CreateInputLayout( vertexlayout, ARRAYSIZE( vertexlayout ), 
                                             g_vert_shader.GetBufferPointer(), g_vert_shader.GetBufferSize(), 
                                             &g_pVertexLayout ) );

	const D3D11_INPUT_ELEMENT_DESC simplevertexlayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,	0, ATTR_OFFS(SFilVert, pos),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

	V_RETURN( pd3dDevice->CreateInputLayout( simplevertexlayout, ARRAYSIZE( simplevertexlayout ), 
                                             g_vert_shader_basic.GetBufferPointer(), g_vert_shader_basic.GetBufferSize(), 
                                             &g_pVertexSimpleLayout ) );


	SetupGroundPlanePipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupBasicSamplePipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupNMapOSPipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupScaleDependentPipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupMixingSamplePipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupPlanarYPipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupTriplanarWSPipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupTriplanarOSPipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupParallaxWithDentsPipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupParallaxWithDetailsPipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupParallaxBasicPipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupPirateExamplePipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupHardSurfacePipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupBumpFromHeightMapPipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupShowTSFromHeightsPipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupFractalsumNoisePipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupTurbulenceNoisePipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupTriplanarPostResolvePipeline(pd3dDevice, pContext, pGlobalsCB);
	SetupTextLabelsPipeline(pd3dDevice, pContext, pGlobalsCB);

	for(int i=0; i<NUM_DRAWABLES; i++)
	{
		for(int j=0; j<NUM_PS_VARIANTS; j++)
		{
			g_ShaderPipelines[j*NUM_DRAWABLES+i].RegisterResourceView("g_vVolumeData", pVolumeDataStructBuf);
			g_ShaderPipelines[j*NUM_DRAWABLES+i].RegisterResourceView("g_decal_norm_tex", g_pTexturesHandler[BASIC_SOFA_NORMALS]);
			g_ShaderPipelines[j*NUM_DRAWABLES+i].RegisterResourceView("g_decal_norm_secondary_tex", g_pTexturesHandler[BASIC_COBBLESTONE_NORMALS]);
			g_ShaderPipelines[j*NUM_DRAWABLES+i].RegisterResourceView("g_decal_cube_norm_tex", g_pTexturesHandler[DECAL_CUBEMAP_OS]);

			g_ShaderPipelines[j*NUM_DRAWABLES+i].RegisterResourceView("g_table_FG", g_pTexturesHandler[TABLE_FG]);
		}
	}


	return res;
}

void PassVolumeIndicesPerTileBuffer(ID3D11ShaderResourceView * pVolumeIndicesPerTile, ID3D11ShaderResourceView * pShadowResolveSRV)
{
	for(int i=0; i<NUM_DRAWABLES; i++)
	{
		for(int j=0; j<NUM_PS_VARIANTS; j++)
		{
			g_ShaderPipelines[j*NUM_DRAWABLES+i].RegisterResourceView("g_vVolumeList", pVolumeIndicesPerTile);
			if(pShadowResolveSRV!=NULL) 
				g_ShaderPipelines[j*NUM_DRAWABLES+i].RegisterResourceView("g_shadowResolve", pShadowResolveSRV);
		}
	}
}

static void RegisterGenericNoiseBuffers(CShaderPipeline &pipe);

static bool GenericPipelineSetup(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB, const Mat44 &mat, const SValueAndStr &sIdxAndStr, const char pixShaderEntryFunc[], const SValueAndStr sMatSizeAndStr)
{
	const int drawableIdx = sIdxAndStr.value; 

	//CONST D3D10_SHADER_MACRO* pDefines = NULL;
	for(int i=0; i<NUM_PS_VARIANTS; i++)
	{
		const char decals_enabled[] = "DECALS_ENABLED";
		const char decals_mip_mapped[] = "DECALS_MIP_MAPPED";

		const bool haveDecals = i==DECALS_ENABLED_MIPMAPPED_ON || i==DECALS_ENABLED_MIPMAPPED_OFF;
		CONST D3D10_SHADER_MACRO sDefines[] = {{sIdxAndStr.str_name, NULL}, {haveDecals ? decals_enabled : NULL, NULL}, {i==DECALS_ENABLED_MIPMAPPED_ON ? decals_mip_mapped : NULL, NULL}, {NULL, NULL}};


		g_pix_shader[i*NUM_DRAWABLES+drawableIdx].CompileShaderFunction(pd3dDevice, L"shader_lighting.hlsl", sDefines, pixShaderEntryFunc, "ps_5_0", g_dwShaderFlags );

		CShaderPipeline &pipe = g_ShaderPipelines[i*NUM_DRAWABLES+drawableIdx];

		// prepare shader pipeline
		pipe.SetVertexShader(&g_vert_shader);
		pipe.SetPixelShader(&g_pix_shader[i*NUM_DRAWABLES+drawableIdx]);

		// register constant buffers
		pipe.RegisterConstBuffer("cbMeshInstance", g_pMeshInstanceCB[drawableIdx]);
		pipe.RegisterConstBuffer("cbGlobals", pGlobalsCB);
		RegisterGenericNoiseBuffers(pipe);
	
		// register samplers
		pipe.RegisterSampler("g_samWrap", GetDefaultSamplerWrap() );
		pipe.RegisterSampler("g_samClamp", GetDefaultSamplerClamp() );
		pipe.RegisterSampler("g_samShadow", GetDefaultShadowSampler() );

	}
	   

	CShaderPipeline &pipeDepth = g_ShaderPipelines_DepthOnly[drawableIdx];

	

	// depth only pipeline
	pipeDepth.SetVertexShader(&g_vert_shader_basic);
	pipeDepth.RegisterConstBuffer("cbMeshInstance", g_pMeshInstanceCB[drawableIdx]);
	pipeDepth.RegisterConstBuffer("cbGlobals", pGlobalsCB);

	HRESULT hr;

	// create constant buffers
	const int byteSizeMatCB = sMatSizeAndStr.value;
	if(byteSizeMatCB>0)
	{
		D3D11_BUFFER_DESC bd;

		memset(&bd, 0, sizeof(bd));
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.ByteWidth = (byteSizeMatCB+0xf)&(~0xf);
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bd.MiscFlags = 0;
		V_RETURN( pd3dDevice->CreateBuffer( &bd, NULL, &g_pMaterialParamsCB[drawableIdx] ) );

		for(int i=0; i<NUM_PS_VARIANTS; i++)
		{
			CShaderPipeline &pipe = g_ShaderPipelines[i*NUM_DRAWABLES+drawableIdx];
			pipe.RegisterConstBuffer(sMatSizeAndStr.str_name, g_pMaterialParamsCB[drawableIdx]);
		}
		pipeDepth.RegisterConstBuffer(sMatSizeAndStr.str_name, g_pMaterialParamsCB[drawableIdx]);
	}

	// update transformation cb
	D3D11_MAPPED_SUBRESOURCE MappedSubResource;
	V( pContext->Map( g_pMeshInstanceCB[drawableIdx], 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedSubResource ) );
	((cbMeshInstance *)MappedSubResource.pData)->g_mLocToWorld = Transpose(mat); 
	((cbMeshInstance *)MappedSubResource.pData)->g_mWorldToLocal = Transpose(~mat); 
    pContext->Unmap( g_pMeshInstanceCB[drawableIdx], 0 );

	// make a record
	g_mLocToWorld[drawableIdx] = mat;

	return true;
}


void RenderSceneGraph(ID3D11DeviceContext *pContext, bool bSimpleLayout, bool bEnableDecals, bool bEnableDecalMipMapping, bool bSkipGroundPlane)
{
	if(bEnableDecals) 
		g_decalMode = bEnableDecalMipMapping ? DECALS_ENABLED_MIPMAPPED_ON : DECALS_ENABLED_MIPMAPPED_OFF;
	else 
		g_decalMode = DECALS_DISABLED;


	for(int i=0; i<NUM_DRAWABLES; i++)
	{
		if((!bSkipGroundPlane) || i!=GROUND_PLANE)
		{
			CShaderPipeline &pipe = g_ShaderPipelines[g_decalMode*NUM_DRAWABLES+i];

			pipe.PrepPipelineForRendering(pContext);
	
			CMeshDraw &mesh = g_pMeshes[ g_meshResourceID[i] ];

			// set streams and layout
			UINT stride = sizeof(SFilVert), offset = 0;
			pContext->IASetVertexBuffers( 0, 1, mesh.GetVertexBuffer(), &stride, &offset );
			pContext->IASetIndexBuffer( mesh.GetIndexBuffer(), DXGI_FORMAT_R32_UINT, 0 );
			pContext->IASetInputLayout( bSimpleLayout ? g_pVertexSimpleLayout : g_pVertexLayout );

			// Set primitive topology
			pContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
			pContext->DrawIndexed( 3*mesh.GetNrTriangles(), 0, 0 );
			pipe.FlushResources(pContext);
		}
	}

	// render labels
	if(!bSimpleLayout)		// skip depth only pass
	{
		CShaderPipeline &pipe = g_LabelsShaderPipelines;

		pipe.PrepPipelineForRendering(pContext);
	
		CMeshDraw &mesh = g_pMeshes[ MESH_LABELS ];

		// set streams and layout
		UINT stride = sizeof(SFilVert), offset = 0;
		pContext->IASetVertexBuffers( 0, 1, mesh.GetVertexBuffer(), &stride, &offset );
		pContext->IASetIndexBuffer( mesh.GetIndexBuffer(), DXGI_FORMAT_R32_UINT, 0 );
		pContext->IASetInputLayout( bSimpleLayout ? g_pVertexSimpleLayout : g_pVertexLayout );

		// Set primitive topology
		pContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		pContext->DrawIndexed( 3*mesh.GetNrTriangles(), 0, 0 );
		pipe.FlushResources(pContext);
	}
}


bool InitializeSceneGraph(ID3D11Device* pd3dDevice, ID3D11DeviceContext *pContext, ID3D11Buffer * pGlobalsCB, ID3D11ShaderResourceView * pVolumeDataStructBuf)
{
	bool res = InitResources(pd3dDevice, pContext, pGlobalsCB, pVolumeDataStructBuf);

	// make the sun
	Mat33 rotX, rotY;
	LoadRotation(&rotX, 30.774*(M_PI/180), 0, 0.0f);
	LoadRotation(&rotY, 0, -30*(M_PI/180), 0.0f);

	Mat33 sunRot2 = rotX * rotY;		// the unity order. Rotates around Y first, then X.
	Mat33 sunRot3 = rotY * rotX;

	// in Lys Azi is 30, Zeni is 31, Y is up and no flips

	//Vec3 g_vSunDir = -Normalize(Vec3(-2.0f,2.0f,-2.5f));
	//Vec3 g_vSunDir = GetColumn(sunRot2, 2); g_vSunDir.x=-g_vSunDir.x;		// matches direction in Unity sample
	g_vSunDir = GetColumn(sunRot3, 2); g_vSunDir.x=-g_vSunDir.x;


	return res;
}

Vec3 GetSunDir()
{
	return g_vSunDir;
}

static CBufferObject g_PermTableBuffer;
static CBufferObject g_GradBuffer;

void ReleaseSceneGraph()
{
	for(int t=0; t<NUM_TEXTURES; t++)
		SAFE_RELEASE( g_pTexturesHandler[t] );

	for(int m=0; m<NUM_MESHES; m++)
		g_pMeshes[m].CleanUp();

	CDXUTResourceCache &cache = DXUTGetGlobalResourceCache();
	cache.OnDestroyDevice();

	g_vert_shader.CleanUp();
	g_vert_shader_basic.CleanUp();
	g_vert_shader_basic_labels.CleanUp();
	g_pix_shader_basic_white.CleanUp();

	for(int i=0; i<NUM_DRAWABLES; i++)
	{
		SAFE_RELEASE( g_pMeshInstanceCB[i] );
		if(g_pMaterialParamsCB[i]!=NULL) SAFE_RELEASE( g_pMaterialParamsCB[i] );
		for(int j=0; j<NUM_PS_VARIANTS; j++)
			g_pix_shader[j*NUM_DRAWABLES+i].CleanUp();
	}
	SAFE_RELEASE( g_pMeshInstanceCB_forLabels );

	SAFE_RELEASE( g_pVertexLayout );
	SAFE_RELEASE( g_pVertexSimpleLayout );

	g_GradBuffer.CleanUp();
	g_PermTableBuffer.CleanUp();
}




static bool CreateNoiseData(ID3D11Device* pd3dDevice)
{
	// 3D version
	static const unsigned char uPermTable[256] =
	{
		151,160,137,91,90,15,
		131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
		190, 6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,
		88,237,149,56,87,174,20,125,136,171,168, 68,175,74,165,71,134,139,48,27,166,
		77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
		102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,200,196,
		135,130,116,188,159,86,164,100,109,198,173,186, 3,64,52,217,226,250,124,123,
		5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,
		223,183,170,213,119,248,152, 2,44,154,163, 70,221,153,101,155,167, 43,172,9,
		129,22,39,253, 19,98,108,110,79,113,224,232,178,185, 112,104,218,246,97,228,
		251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107,
		49,192,214, 31,181,199,106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,
		138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
	};

	static const Vec3 grads_array[] =
	{
		Vec3(1,1,0), Vec3(-1,1,0), Vec3(1,-1,0), Vec3(-1,-1,0),
		Vec3(1,0,1), Vec3(-1,0,1), Vec3(1,0,-1), Vec3(-1,0,-1),
		Vec3(0,1,1), Vec3(0,-1,1), Vec3(0,1,-1), Vec3(0,-1,-1),
		Vec3(1,1,0), Vec3(0,-1,1), Vec3(-1,1,0), Vec3(0,-1,-1)
	};

	bool res = true;
	res &= g_PermTableBuffer.CreateBuffer(pd3dDevice, 256, 0, uPermTable, CBufferObject::DefaultBuf, true, false);
	res &= g_PermTableBuffer.AddTypedSRV(pd3dDevice, DXGI_FORMAT_R8_UINT);

	res &= g_GradBuffer.CreateBuffer(pd3dDevice, 16*sizeof(Vec3), 0, grads_array, CBufferObject::DefaultBuf, true, false);
	res &= g_GradBuffer.AddTypedSRV(pd3dDevice, DXGI_FORMAT_R32G32B32_FLOAT);

	return res;
}


static void RegisterGenericNoiseBuffers(CShaderPipeline &pipe)
{
	pipe.RegisterResourceView("g_uPermTable", g_PermTableBuffer.GetSRV());
	pipe.RegisterResourceView("g_v3GradArray", g_GradBuffer.GetSRV());
}


// shadow support functions
int GetNumberOfShadowCastingMeshInstances()
{
	return NUM_DRAWABLES-1;		// minus one to exclude the groundplane
}

void GetAABBoxAndTransformOfShadowCastingMeshInstance(Vec3 * pvMin, Vec3 * pvMax, Mat44 * pmMat, const int idx_in)
{
	const int idx = idx_in+1;		// skip groundplane
	assert(idx>=0 && idx<NUM_DRAWABLES);

	const int mesh_idx = g_meshResourceID[idx];


	CMeshDraw &mesh = g_pMeshes[mesh_idx];

	*pvMin = mesh.GetMin();
	*pvMax = mesh.GetMax();
	*pmMat = g_mLocToWorld[idx];
}