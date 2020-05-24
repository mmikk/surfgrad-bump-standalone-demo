#ifndef __STDCBUFFER_H__
#define __STDCBUFFER_H__

#include "shader_base.h"


unistruct cbMeshInstance
{
	Mat44 g_mLocToWorld;
	Mat44 g_mWorldToLocal;
};

unistruct cbGlobals
{
	Vec3	g_vCamPos;
	int		g_iMode;

	Mat44	g_mProj;
	Mat44	g_mViewProjection;
	Mat44	g_mWorldToView;
	Mat44	g_mViewToWorld;
	Mat44	g_mScrToView;

	int		g_iWidth;
	int		g_iHeight;

	// though additive vs. masked is a global setting in this demo it can be set per decal
	int		g_iDecalBlendingMethod;		// 0 - additive surfgrad, 1 - masked surfgrad, 2 - decal direction as new normal
	int		g_bShowNormalsWS;
	Vec3	g_vSunDir;
	int		g_bEnableShadows;

	int		g_iBumpFromHeightMapMethod;
	int		g_bUseSecondaryUVsetOnPirate;
};

#endif