#ifndef __CUSTOMCBUFFERS_H__
#define __CUSTOMCBUFFERS_H__

#include "shader_base.h"


#if defined(BASIC_SAMPLE) || defined(__cplusplus)
unistruct cbMatBasicShader
{
	float g_fTileRate;
	float g_fBumpIntensity;
	int g_bUseVertexTSpace;
};
#endif

#if defined(NMAP_OS) || defined(__cplusplus)
unistruct cbMatNMapOSShader
{
	float g_fBumpIntensity;
};
#endif

#if defined(SCALE_DEPENDENT) || defined(__cplusplus)
unistruct cbMatScaleDependentShader
{
	float g_fTileRate;
	float g_fBumpIntensity;
};
#endif

#if defined(MIXING_SAMPLE) || defined(__cplusplus)
unistruct cbMatMixingShader
{
	float g_fBaseBumpScale;
	float g_fDetailBumpScale;
	float g_fDetailTileRate;
	int g_bUseSecondaryUVsForDetails;
	float g_fNoiseTileRate;
	float g_fNoiseBumpScale;
	int g_bInvertBumpping;
};
#endif

#if defined(PLANAR_Y) || defined(__cplusplus)
unistruct cbMatPlanarY
{
	float g_fBumpIntensity;
	float g_fTileRate;
};
#endif

#if defined(TRIPLANAR_OS) || defined(TRIPLANAR_WS) || defined(__cplusplus)
unistruct cbMatTriplanar
{
	float g_fBumpIntensity;
	float g_fTileRate;
};
#endif



#if defined(PARALLAX_BASIC) || defined(__cplusplus)
unistruct cbMatParallaxBasic
{
	float g_fTileRate;
	float g_fBumpScale;
};
#endif

#if defined(PARALLAX_DETAILS) || defined(__cplusplus)
unistruct cbMatParallaxDetails
{
	float g_fTileRate;
	float g_fBumpScale;
	float g_fDetailTileRate;
};
#endif

#if defined(PARALLAX_DENTS) || defined(__cplusplus)
unistruct cbMatParallaxDents
{
	float g_fTileRate;
	float g_fBumpScale;
	float g_fNoiseTileRate;
	float g_fNoiseBumpScale;

	Vec3 g_vVolumeBumpOffset;
	float g_fVolumeBumpPowerValue;
};
#endif


#if defined(BUMP_FROM_HEIGHTMAP) || defined(__cplusplus)
unistruct cbMatBumpFromHeightShader
{
	float g_fTileRate;
	float g_fBumpIntensity;
	//int g_bUseUpscaleHQ;
};
#endif

#if defined(SHOW_TS_FROM_HEIGHTMAP) || defined(__cplusplus)
unistruct cbMatShowFromHeightsTS
{
	float g_fTileRate;
	float g_fBumpIntensity;
	int g_iSamplingMethod;		// 0 - upscaleHQ, 1 - 3 tap, 2 - from normal map
};
#endif


#if defined(BUMP_FROM_FRACTALSUM_NOISE_3D) || defined(BUMP_FROM_TURBULENCE_NOISE_3D) || defined(__cplusplus)
unistruct cbMatNoise
{
	float g_fNoiseTileRate;
	float g_fNoiseBumpScale;
};
#endif

#if defined(TRIPLANAR_POST) || defined(__cplusplus)
unistruct cbMatTriplanarPost
{
	float g_fBaseTileRate;
	float g_fBaseBumpScale;
	float g_fTileRate;
	float g_fBumpIntensity;
};
#endif


#if defined(PIRATE_EXAMPLE) || defined(__cplusplus)
unistruct cbMatPirateShader
{
	float g_fDetailBumpScale;
	float g_fDetailTileRate;
	//int g_bUseSecondaryUVForDetailMap;
	float g_fHairNoiseBumpScale;
	float g_fHairNoiseTileRate;
	int g_bHairNoisePostResolve;
	int g_iTSpaceMode;		// 0 - vertex tspace,  1 - on the fly tspace, 2 - object space nmap
};
#endif






#endif