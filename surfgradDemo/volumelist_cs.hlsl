#include "volume_definitions.h"
#include "SortingComputeUtils.h"


Texture2D g_depth_tex : register( t0 );
StructuredBuffer<float3> g_vBoundsBuffer : register( t1 );
StructuredBuffer<SFiniteVolumeData> g_vVolumeData : register( t2 );
StructuredBuffer<SFiniteVolumeBound> g_data : register( t3 );


#define NR_THREADS			64

#define PERFORM_SPHERICAL_INTERSECTION_TESTS

// output buffer
RWBuffer<uint> g_vVolumeList : register( u0 );

#define MAX_NR_COARSE_ENTRIES		64
#define MAX_NR_PRUNED_ENTRIES		24

groupshared unsigned int coarseList[MAX_NR_COARSE_ENTRIES];
groupshared unsigned int prunedList[MAX_NR_COARSE_ENTRIES];		// temporarily support room for all 64 while in LDS

groupshared uint ldsZMin;
groupshared uint ldsZMax;
groupshared uint volumeOffs;
#ifdef FINE_PRUNING_ENABLED
groupshared uint ldsDoesVolumeIntersect[2];
#endif
groupshared int ldsNrVolumesFinal;


#ifdef PERFORM_SPHERICAL_INTERSECTION_TESTS
groupshared uint volumeOffsSph;
#endif

float GetLinearDepth(float3 vP)
{
	float4 v4Pres = mul(float4(vP,1.0), g_mInvScrProjection);
	return v4Pres.z / v4Pres.w;
}


float3 GetViewPosFromLinDepth(float2 v2ScrPos, float fLinDepth)
{
	float fSx = g_mScrProjection[0].x;
	float fCx = g_mScrProjection[2].x;
	float fSy = g_mScrProjection[1].y;
	float fCy = g_mScrProjection[2].y;

#ifdef LEFT_HAND_COORDINATES
	return fLinDepth*float3( ((v2ScrPos.x-fCx)/fSx), ((v2ScrPos.y-fCy)/fSy), 1.0 );
#else
	return fLinDepth*float3( -((v2ScrPos.x+fCx)/fSx), -((v2ScrPos.y+fCy)/fSy), 1.0 );
#endif
}

float GetOnePixDiagWorldDistAtDepthOne()
{
    float fSx = g_mScrProjection[0].x;
    float fSy = g_mScrProjection[1].y;

    return length( float2(1.0/fSx,1.0/fSy) );
}

#ifdef PERFORM_SPHERICAL_INTERSECTION_TESTS
int SphericalIntersectionTests(uint threadID, int iNrCoarseVolumes, float2 screenCoordinate);
#endif

#ifdef FINE_PRUNING_ENABLED
void FinePruneVolumes(uint threadID, int iNrCoarseVolumes, uint2 viTilLL, uint iWidth, uint iHeight);
#endif


[numthreads(NR_THREADS, 1, 1)]
void main(uint threadID : SV_GroupIndex, uint3 u3GroupID : SV_GroupID)
{
	uint2 tileIDX = u3GroupID.xy;
	uint t=threadID;

	if(t<MAX_NR_COARSE_ENTRIES)
		prunedList[t]=0;
	
	
	uint iWidth;
	uint iHeight;
	g_depth_tex.GetDimensions(iWidth, iHeight);
	uint nrTilesX = (iWidth+15)/16;
	uint nrTilesY = (iHeight+15)/16;

	// build tile scr boundary
	const uint uFltMax = 0x7f7fffff;  // FLT_MAX as a uint
	if(t==0)
	{
		ldsZMin = uFltMax;
		ldsZMax = 0;
		volumeOffs = 0;
	}

#if !defined(XBONE) && !defined(PLAYSTATION4)
	GroupMemoryBarrierWithGroupSync();
#endif


	uint2 viTilLL = 16*tileIDX;

	// establish min and max depth first
	float dpt_mi=asfloat(uFltMax), dpt_ma=0.0;

	for(int idx=t; idx<256; idx+=NR_THREADS)
	{
		uint2 uCrd = min( uint2(viTilLL.x+(idx&0xf), viTilLL.y+(idx>>4)), uint2(iWidth-1, iHeight-1) );
		const float fDpth = g_depth_tex.Load( uint3(uCrd.xy, 0) ).x;
		if(fDpth<VIEWPORT_SCALE_Z)		// if not skydome
		{
			dpt_mi = min(fDpth, dpt_mi);
			dpt_ma = max(fDpth, dpt_ma);
		}
	}

	InterlockedMax(ldsZMax, asuint(dpt_ma) );
	InterlockedMin(ldsZMin, asuint(dpt_mi) );


#if !defined(XBONE) && !defined(PLAYSTATION4)
	GroupMemoryBarrierWithGroupSync();
#endif


	float3 vTileLL = float3(viTilLL.x/(float) iWidth, viTilLL.y/(float) iHeight, asfloat(ldsZMin));
	float3 vTileUR = float3((viTilLL.x+16)/(float) iWidth, (viTilLL.y+16)/(float) iHeight, asfloat(ldsZMax));
	vTileUR.xy = min(vTileUR.xy,float2(1.0,1.0)).xy;


	// build coarse list using AABB
	for(int l=(int) t; l<(int) g_iNrVisibVolumes; l += NR_THREADS)
	{
		const float3 vMi = g_vBoundsBuffer[l];
		const float3 vMa = g_vBoundsBuffer[l+g_iNrVisibVolumes];

		if( all(vMa>vTileLL) && all(vMi<vTileUR))
		{
			unsigned int uInc = 1;
			unsigned int uIndex;
			InterlockedAdd(volumeOffs, uInc, uIndex);
			if(uIndex<MAX_NR_COARSE_ENTRIES) coarseList[uIndex] = l;		// add to list
		}
	}

#ifdef FINE_PRUNING_ENABLED	
	if(t<2) ldsDoesVolumeIntersect[t] = 0;
#endif

#if !defined(XBONE) && !defined(PLAYSTATION4)
	GroupMemoryBarrierWithGroupSync();
#endif

	int iNrCoarseVolumes = min(volumeOffs,MAX_NR_COARSE_ENTRIES);

#ifdef PERFORM_SPHERICAL_INTERSECTION_TESTS
    iNrCoarseVolumes = SphericalIntersectionTests( t, iNrCoarseVolumes, float2(min(viTilLL.xy+uint2(16/2,16/2), uint2(iWidth-1, iHeight-1))) );
#endif

#ifndef FINE_PRUNING_ENABLED	
	{
		int iNrVolumesOut = iNrCoarseVolumes<MAX_NR_PRUNED_ENTRIES ? iNrCoarseVolumes : MAX_NR_PRUNED_ENTRIES;
		if((int)t<iNrVolumesOut) prunedList[t] = coarseList[t];
		if(t==0) ldsNrVolumesFinal=iNrVolumesOut;
	}
#else
	{
		// initializes ldsNrVolumesFinal with the number of accepted volumes.
        // all accepted entries delivered in prunedList[].
        FinePruneVolumes(t, iNrCoarseVolumes, viTilLL, iWidth, iHeight);
	}
#endif


#if !defined(XBONE) && !defined(PLAYSTATION4)
	GroupMemoryBarrierWithGroupSync();
#endif

	int nrVolumesCombinedList = min(ldsNrVolumesFinal,MAX_NR_COARSE_ENTRIES);

	// for blending decals we need consistent ordering
#if !defined(XBONE) && !defined(PLAYSTATION4)
	SORTLIST(prunedList, nrVolumesCombinedList, MAX_NR_COARSE_ENTRIES, t, NR_THREADS);
#endif

	// no more than 24 pruned volumes go out
	int nrVolumesFinal = min(nrVolumesCombinedList,MAX_NR_PRUNED_ENTRIES);

	int offs = tileIDX.y*nrTilesX + tileIDX.x;

	for(int l=t; l<(nrVolumesFinal+1); l++)
	{
		g_vVolumeList[NR_USHORTS_PER_TILE*offs + l] = l==0 ? nrVolumesFinal : prunedList[max(0,l - 1)];
	}
}



#ifdef PERFORM_SPHERICAL_INTERSECTION_TESTS
bool DoesSphereOverlapTile(float3 dir, float halfTileSizeAtZDistOne, float3 sphCen_in, float sphRadiusIn, bool isOrthographic)
{
    float3 V = float3(isOrthographic ? 0.0 : dir.x, isOrthographic ? 0.0 : dir.y, dir.z);     // ray direction down center of tile (does not need to be normalized).
    float3 sphCen = float3(sphCen_in.x - (isOrthographic ? dir.x : 0.0), sphCen_in.y - (isOrthographic ? dir.y : 0.0), sphCen_in.z);

#if 1
    float3 maxZdir = float3(-sphCen.z*sphCen.x, -sphCen.z*sphCen.y, sphCen.x*sphCen.x + sphCen.y*sphCen.y);     // cross(sphCen,cross(Zaxis,sphCen))
    float len = length(maxZdir);
    float scalarProj = len>0.0001 ? (maxZdir.z/len) : len;  // if len<=0.0001 then either |sphCen|<sphRadius or sphCen is very closely aligned with Z axis in which case little to no additional offs needed.
    float offs = scalarProj*sphRadiusIn;
#else
    float offs = sphRadiusIn;       // more false positives due to larger radius but works too
#endif

    // enlarge sphere so it overlaps the center of the tile assuming it overlaps the tile to begin with.
#ifdef LEFT_HAND_COORDINATES
    float s = sphCen.z+offs;
#else
    float s = -(sphCen.z-offs);
#endif
    float sphRadius = sphRadiusIn + (isOrthographic ? 1.0 : s)*halfTileSizeAtZDistOne;

    float a = dot(V,V);
    float CdotV = dot(sphCen,V);
    float c = dot(sphCen,sphCen) - sphRadius*sphRadius;

    float fDescDivFour = CdotV*CdotV - a*c;

    return c<0 || (fDescDivFour>0 && CdotV>0);      // if ray hits bounding sphere
}


int SphericalIntersectionTests(uint threadID, int iNrCoarseVolumes, float2 screenCoordinate)
{
    if(threadID==0) volumeOffsSph = 0;

    // make a copy of coarseList in prunedList.
    int l;
    for(l=threadID; l<iNrCoarseVolumes; l+=NR_THREADS)
        prunedList[l]=coarseList[l];

#if !defined(XBONE) && !defined(PLAYSTATION4)
    GroupMemoryBarrierWithGroupSync();
#endif

#ifdef LEFT_HAND_COORDINATES
    float3 V = GetViewPosFromLinDepth( screenCoordinate, 1.0);
#else
    float3 V = GetViewPosFromLinDepth( screenCoordinate, -1.0);
#endif

    float onePixDiagDist = GetOnePixDiagWorldDistAtDepthOne();
    float halfTileSizeAtZDistOne = 8*onePixDiagDist;        // scale by half a tile

    for(l=threadID; l<iNrCoarseVolumes; l+=NR_THREADS)
    {
        SFiniteVolumeBound volumeData = g_data[prunedList[l]];

        if( DoesSphereOverlapTile(V, halfTileSizeAtZDistOne, volumeData.vCen.xyz, volumeData.fRadius, false) )
        {
            unsigned int uIndex;
            InterlockedAdd(volumeOffsSph, 1, uIndex);
            coarseList[uIndex]=prunedList[l];       // read from the original copy of coarseList which is backed up in prunedList
        }
    }

#if !defined(XBONE) && !defined(PLAYSTATION4)
    GroupMemoryBarrierWithGroupSync();
#endif

    return volumeOffsSph;
}
#endif



#ifdef FINE_PRUNING_ENABLED
void FinePruneVolumes(uint threadID, int iNrCoarseVolumes, uint2 viTilLL, uint iWidth, uint iHeight)
{
	uint t = threadID;

	float4 vLinDepths;
	for(int i=0; i<4; i++)
	{
		int idx = t + i*NR_THREADS;

		uint2 uCrd = min( uint2(viTilLL.x+(idx&0xf), viTilLL.y+(idx>>4)), uint2(iWidth-1, iHeight-1) );
		float3 v3ScrPos = float3(uCrd.x+0.5, uCrd.y+0.5, g_depth_tex.Load( uint3(uCrd.xy, 0) ).x);
		vLinDepths[i] = GetLinearDepth(v3ScrPos);
	}

	uint uVolumesFlags[2] = {0,0};
	int l=0;
	// we need this outer loop for when we cannot assume a wavefront is 64 wide
	// since in this case we cannot assume the volumes will remain sorted by type
#if !defined(XBONE) && !defined(PLAYSTATION4)
	while(l<iNrCoarseVolumes)
#endif
	{
		// fetch volume
		int idxCoarse = l<iNrCoarseVolumes ? coarseList[l] : 0;
		uint uVolType = l<iNrCoarseVolumes ? g_vVolumeData[idxCoarse].uVolumeType : 0;

		// spot and wedge volumes
		while(l<iNrCoarseVolumes && (uVolType==SPOT_CIRCULAR_VOLUME || uVolType==WEDGE_VOLUME))
		{
			SFiniteVolumeData volDat = g_vVolumeData[idxCoarse];

			float3 Ldir = -volDat.vAxisY.xyz;

			const float fSpotNearPlane = 0;		// don't have one right now

			// serially check 4 pixels
			uint uVal = 0;
			for(int i=0; i<4; i++)
			{
				int idx = t + i*NR_THREADS;
	
				uint2 uPixLoc = min(uint2(viTilLL.x+(idx&0xf), viTilLL.y+(idx>>4)), uint2(iWidth-1, iHeight-1));
                float3 vVPos = GetViewPosFromLinDepth(uPixLoc + float2(0.5,0.5), vLinDepths[i]);
	
				// check pixel
				float3 fromVolume = vVPos-volDat.vLpos.xyz;
				if(uVolType==WEDGE_VOLUME) fromVolume -= clamp(dot(fromVolume, volDat.vAxisX.xyz), 0, volDat.fSegLength) * volDat.vAxisX.xyz;
				float distSq = dot(fromVolume,fromVolume);
				const float fProjVecMag = dot(fromVolume, Ldir);

				if( all( float3(volDat.fSphRadiusSq, fProjVecMag, fProjVecMag) > float3(distSq, sqrt(distSq)*volDat.fPenumbra, fSpotNearPlane) ) ) uVal = 1;
			}

			uVolumesFlags[l<32 ? 0 : 1] |= (uVal<<(l&31));
			++l; idxCoarse = l<iNrCoarseVolumes ? coarseList[l] : 0;
			uVolType = l<iNrCoarseVolumes ? g_vVolumeData[idxCoarse].uVolumeType : 0;
		}

		// sphere and capsule test
		while(l<iNrCoarseVolumes && (uVolType==SPHERE_VOLUME || uVolType==CAPSULE_VOLUME))
		{
			SFiniteVolumeData volDat = g_vVolumeData[idxCoarse];

			// serially check 4 pixels
			uint uVal = 0;
			for(int i=0; i<4; i++)
			{
				int idx = t + i*NR_THREADS;
	
				uint2 uPixLoc = min(uint2(viTilLL.x+(idx&0xf), viTilLL.y+(idx>>4)), uint2(iWidth-1, iHeight-1));
                float3 vVPos = GetViewPosFromLinDepth(uPixLoc + float2(0.5,0.5), vLinDepths[i]);
	
				// check pixel
				float3 vLp = volDat.vLpos.xyz;
				if(uVolType==CAPSULE_VOLUME) vLp += clamp(dot(vVPos-vLp, volDat.vAxisX.xyz), 0, volDat.fSegLength) * volDat.vAxisX.xyz;
				float3 toVolume = vLp - vVPos; 
				float distSq = dot(toVolume,toVolume);
			
				if(distSq<volDat.fSphRadiusSq) uVal = 1;
			}

			uVolumesFlags[l<32 ? 0 : 1] |= (uVal<<(l&31));
			++l; idxCoarse = l<iNrCoarseVolumes ? coarseList[l] : 0;
			uVolType = l<iNrCoarseVolumes ? g_vVolumeData[idxCoarse].uVolumeType : 0;
		}


		// box test
		while(l<iNrCoarseVolumes && uVolType==BOX_VOLUME)
		{
			SFiniteVolumeData volDat = g_vVolumeData[idxCoarse];

			// serially check 4 pixels
			uint uVal = 0;
			for(int i=0; i<4; i++)
			{
				int idx = t + i*NR_THREADS;
	
				uint2 uPixLoc = min(uint2(viTilLL.x+(idx&0xf), viTilLL.y+(idx>>4)), uint2(iWidth-1, iHeight-1));
                float3 vVPos = GetViewPosFromLinDepth(uPixLoc + float2(0.5,0.5), vLinDepths[i]);

				float3 toVolume  = volDat.vLpos.xyz - vVPos;

				float3 dist = float3( dot(toVolume, volDat.vAxisX), dot(toVolume, volDat.vAxisY.xyz), dot(toVolume, volDat.vAxisZ) );
				dist = (abs(dist) - volDat.vBoxInnerDist) * volDat.vBoxInvRange;		// not as efficient as it could be
				if( max(max(dist.x, dist.y), dist.z)<1 ) uVal = 1;						// but allows us to not write out OuterDists
			}

			uVolumesFlags[l<32 ? 0 : 1] |= (uVal<<(l&31));
			++l; idxCoarse = l<iNrCoarseVolumes ? coarseList[l] : 0;
			uVolType = l<iNrCoarseVolumes ? g_vVolumeData[idxCoarse].uVolumeType : 0;
		}

#if !defined(XBONE) && !defined(PLAYSTATION4)
		// in case we have some corrupt data make sure we terminate
		if(uVolType>=MAX_TYPES) ++l;
#endif
	}

	InterlockedOr(ldsDoesVolumeIntersect[0], uVolumesFlags[0]);
	InterlockedOr(ldsDoesVolumeIntersect[1], uVolumesFlags[1]);
	if(t==0) ldsNrVolumesFinal = 0;

#if !defined(XBONE) && !defined(PLAYSTATION4)
	GroupMemoryBarrierWithGroupSync();
#endif

	if(t<iNrCoarseVolumes && (ldsDoesVolumeIntersect[t<32 ? 0 : 1]&(1<<(t&31)))!=0 )
	{
		unsigned int uInc = 1;
		unsigned int uIndex;
		InterlockedAdd(ldsNrVolumesFinal, uInc, uIndex);
		if(uIndex<MAX_NR_COARSE_ENTRIES) prunedList[uIndex] = coarseList[t];		// we allow up to 64 pruned volumes while stored in LDS.
	}
}
#endif