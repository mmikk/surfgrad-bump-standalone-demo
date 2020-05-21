#ifndef __VOLUME_DEFINITIONS_H__
#define __VOLUME_DEFINITIONS_H__


#include "shader_base.h"


#define MAX_NR_VOLUMES_PER_CAMERA		1024

#define NR_USHORTS_PER_TILE				32

//#define LEFT_HAND_COORDINATES
#define VIEWPORT_SCALE_Z				1.0

unistruct cbBoundsInfo	_CB_REGSLOT(b0)
{
	Mat44 g_mProjection;
	Mat44 g_mInvProjection;
	Mat44 g_mScrProjection;
	Mat44 g_mInvScrProjection;

	unsigned int g_iNrVisibVolumes;
	Vec3 g_vStuff;
};


// Volume types
#define MAX_TYPES					5

#define SPOT_CIRCULAR_VOLUME		0
#define WEDGE_VOLUME				1
#define SPHERE_VOLUME				2
#define CAPSULE_VOLUME				3
#define BOX_VOLUME					4


struct SFiniteVolumeData
{
	 // setup constant buffer
    float fNearRadiusOverRange_LP0;
    float fInvRange;
    float fPenumbra;
    float fInvUmbraDelta;
    
    Vec3 vLpos;
    float fSphRadiusSq;
    
    Vec3 vAxisX;
    unsigned int uVolumeType;
    
    Vec3 vAxisY;
    float fSegLength;
    
    Vec3 vAxisZ;
	float	fPad0;
    
    Vec4 vBoxInnerDist;
	
    Vec4 vBoxInvRange;	

	Vec3	vCol;
	float	fPad1;
};

struct SFiniteVolumeBound
{
	Vec3 vBoxAxisX;
	Vec3 vBoxAxisY;
	Vec3 vBoxAxisZ;
	Vec3 vCen;
	Vec2 vScaleXZ;
	float fRadius;
};


#endif