#include "std_cbuffer.h"
#include "volume_definitions.h"
#include "debug_volume_base.h"

StructuredBuffer<SFiniteVolumeData> g_vVolumeData;

SamplerState g_samWrap;
SamplerState g_samClamp;
SamplerComparisonState g_samShadow;


#ifndef M_PI
	#define M_PI 3.1415926535897932384626433832795
#endif



//--------------------------------------------------------------------------------------
// shader input/output structure
//--------------------------------------------------------------------------------------
struct VS_INPUT
{
	uint vert_id : SV_VertexID;
    uint instance_id : SV_InstanceID;
};

struct VS_OUTPUT
{
    float4 Position     : SV_POSITION;
};

void GenBoxVertex(out float3 Po, out bool assignedToPrim_o, VS_INPUT input, SFiniteVolumeData data);
void GenSphereVertex(out float3 Po, out bool assignedToPrim_o, VS_INPUT input, SFiniteVolumeData data);
void GenSpotVertex(out float3 Po, out bool assignedToPrim_o, VS_INPUT input, SFiniteVolumeData data);
void GenWedgeVertex(out float3 Po, out bool assignedToPrim_o, VS_INPUT input, SFiniteVolumeData data);
void GenCapsuleVertex(out float3 Po, out bool assignedToPrim_o, VS_INPUT input, SFiniteVolumeData data);

VS_OUTPUT RenderDebugVolumeVS( VS_INPUT input)
{
	VS_OUTPUT Output;

	SFiniteVolumeData data = g_vVolumeData[input.instance_id];

	bool assignedToPrim = false;
	
	float3 P_c;
	if(data.uVolumeType==BOX_VOLUME)
	{
		GenBoxVertex(P_c, assignedToPrim, input, data);
	}
	else if(data.uVolumeType==SPHERE_VOLUME)
	{
		GenSphereVertex(P_c, assignedToPrim, input, data);
	}
	else if(data.uVolumeType==SPOT_CIRCULAR_VOLUME)
	{
		GenSpotVertex(P_c, assignedToPrim, input, data);
	}
	else if(data.uVolumeType==WEDGE_VOLUME)
	{
		GenWedgeVertex(P_c, assignedToPrim, input, data);
	}
	else if(data.uVolumeType==CAPSULE_VOLUME)
	{
		GenCapsuleVertex(P_c, assignedToPrim, input, data);
	}


	float4 P;
	if(assignedToPrim)
	{
		P_c *= 0.9995;	   // bias
	
		// Transform the position from object space to homogeneous projection space
		P = mul( float4(P_c,1.0), g_mProj );
	}
	else
	{
		float fNaN = asfloat(0xFFC00000U);
		P = float4(fNaN, fNaN, fNaN, fNaN);
	}

	Output.Position = P;

	return Output;    
}


float4 YellowPS( VS_OUTPUT In ) : SV_TARGET0
{
	return float4(1,1,0,1);
}

float3 GetVolumeBoxSize(SFiniteVolumeData volDat)
{
	return volDat.vBoxInnerDist + (1.0 / volDat.vBoxInvRange);
}

float GetVolumeRadius(SFiniteVolumeData volDat)
{
	return -volDat.fNearRadiusOverRange_LP0 / volDat.fInvRange;
}

void GenBoxVertex(out float3 Po, out bool assignedToPrim_o, VS_INPUT input, SFiniteVolumeData data)
{
	uint uID = input.vert_id;		// 0, 1, ..., 23
	//uint uIDtmp = input.vert_id;		// 0, 1, ..., 23

	// lazy way to make this work with polyline
	//uint uIDdiv3 = uIDtmp / 3;
	//uint rem = uIDtmp-3*uIDdiv3;
	//uint uID = 2*uIDdiv3 + rem;

	//if(uID<24 && rem<3)
	if(uID<24)
	{
		uint uLineSegmentID = uID>>1;	// 0, 1, ...., 11
		uint uAxis = uLineSegmentID>>2;	// 0 = X, 1 = Y or 2 = Z

		uint uAxisLocalSegmentID = uLineSegmentID&0x3;

		float3 vP = float3((uID&0x1)!=0 ? -1.0 : 1.0, -1.0, 1.0);

		// rotate by a multiple of 90 degrees around X axis 0, 90, 180 or 270
		vP.yz = (uAxisLocalSegmentID&0x1)==0 ? vP.yz : vP.zy;
		vP.y = (((uAxisLocalSegmentID+1)>>1)&0x1)!=0 ? -vP.y : vP.y;
		vP.z = (((uAxisLocalSegmentID+0)>>1)&0x1)!=0 ? -vP.z : vP.z;

		// X, Y or Z axis 
		vP = uAxis==2 ? float3(vP.zy, -vP.x) : (uAxis==1 ? float3(-vP.y, vP.xz) : vP);

		// transform to view space
		float3 vSize = GetVolumeBoxSize(data);

		vP = vP * vSize;
		Po = data.vLpos.xyz + mul(vP, float3x3(data.vAxisX, data.vAxisY, data.vAxisZ));
		 
		assignedToPrim_o = true;
	}
}

void GenSphereVertex(out float3 Po, out bool assignedToPrim_o, VS_INPUT input, SFiniteVolumeData data)
{
	uint uID = input.vert_id;
	if(uID<(2*NR_SEGMENTS_ON_SPHERE))
	{
		const uint nrSegmentsOnFullCircle = NR_SEGMENTS_ON_HALF_CIRCLE*2;
		uint uSegID = uID>>1;
		uint uAxis = uSegID / nrSegmentsOnFullCircle;	// 0 = X, 1 = Y or 2 = Z

		uint uLocalSegID = uSegID - uAxis*nrSegmentsOnFullCircle;
		float t = ((float) (uLocalSegID + (uID&0x1))) / ((float) nrSegmentsOnFullCircle);
		float angle = 2*M_PI*t;

		float cs = cos(angle), si = sin(angle);
		float3 vP = float3(0.0, si, cs);

		// X, Y or Z axis 
		vP = uAxis==2 ? float3(vP.zy, -vP.x) : (uAxis==1 ? float3(-vP.y, vP.xz) : vP);

		// transform to view space
		float radius = GetVolumeRadius(data);
		vP = vP * radius;
		Po = data.vLpos.xyz + mul(vP, float3x3(data.vAxisX, data.vAxisY, data.vAxisZ));
		 
	
		assignedToPrim_o = true;
	}
}

void GenCapsuleVertex(out float3 Po, out bool assignedToPrim_o, VS_INPUT input, SFiniteVolumeData data)
{
	uint uID = input.vert_id;
	if(uID<(2*NR_SEGMENTS_ON_CAPSULE))
	{
		float radius = GetVolumeRadius(data);
		float segLen = data.fSegLength;

		uint uSegID = uID>>1;
		float3 vP = 0.0;

		if(uSegID<4)
		{
			vP = float3((uID&0x1)!=0 ? segLen : 0.0, 0.0, radius);

			// rotate by a multiple of 90 degrees around X axis 0, 90, 180 or 270
			vP.yz = (uSegID&0x1)==0 ? vP.yz : vP.zy;
			vP.y = (((uSegID+1)>>1)&0x1)!=0 ? -vP.y : vP.y;
			vP.z = (((uSegID+0)>>1)&0x1)!=0 ? -vP.z : vP.z;
		}
		else
		{
			uint uSegHalfCircs = uSegID-4;

			const uint nrCurveSegsOnOneSide = 4*NR_SEGMENTS_ON_HALF_CIRCLE;
			bool isMirror = uSegHalfCircs>=nrCurveSegsOnOneSide;
			uint uSegIDHalfCircsOneHalf = isMirror ? (uSegHalfCircs-nrCurveSegsOnOneSide) : uSegHalfCircs;

				   ///////////////
			uint uHalfCircPiece = uSegIDHalfCircsOneHalf / NR_SEGMENTS_ON_HALF_CIRCLE;
			uint uLocalSegID = uSegIDHalfCircsOneHalf - uHalfCircPiece*NR_SEGMENTS_ON_HALF_CIRCLE;

			float t = ((float) (uLocalSegID + (uID&0x1))) / ((float) NR_SEGMENTS_ON_HALF_CIRCLE);
			float angle = M_PI*t;

			uint uAxis = uHalfCircPiece<3 ? uHalfCircPiece : 0;

			float cs = cos(angle), si = sin(angle);
			vP = float3(0.0, si, cs);

			// X, Y or Z axis 
			vP = uAxis==2 ? float3(-vP.y, vP.z, -vP.x) : (uAxis==1 ? float3(-vP.y, vP.xz) : vP);
			if(uHalfCircPiece==3) vP.yz = -vP.yz;

			vP = vP * radius;
			if(isMirror)
			{
				vP.xz = -vP.xz; vP.x += segLen;
			}
		}

		// transform to view space
		Po = data.vLpos.xyz + mul(vP, float3x3(data.vAxisX, data.vAxisY, data.vAxisZ));
		assignedToPrim_o = true;
	}
}


void GenSpotVertex(out float3 Po, out bool assignedToPrim_o, VS_INPUT input, SFiniteVolumeData data)
{
	uint uID = input.vert_id;
	if(uID<(2*NR_SEGMENTS_ON_SPOT))
	{
		float radius = GetVolumeRadius(data);
		float cosHalfFov = data.fPenumbra;
		float sinHalfFov = sqrt(max(0.0, 1-cosHalfFov*cosHalfFov));
		float halfFov = acos(cosHalfFov);

		uint uSegID = uID>>1;

		float3 vP = 0.0;

		if(uSegID<4)
		{
			// rotate by a multiple of 90 degrees around X axis 0, 90, 180 or 270
			vP = radius * float3(-sinHalfFov, -cosHalfFov, 0.0);

			// rotate by a multiple of 90 degrees around Y axis 0, 90, 180 or 270
			vP.zx = (uSegID&0x1)==0 ? vP.zx : vP.xz;
			vP.z = (((uSegID+1)>>1)&0x1)!=0 ? -vP.z : vP.z;
			vP.x = (((uSegID+0)>>1)&0x1)!=0 ? -vP.x : vP.x;

			if((uID&0x1)==0) vP=0.0;
		}
		else
		{
			uint uSegCircs = uSegID-4;
			uint uHalfCircPiece = uSegCircs / NR_SEGMENTS_ON_HALF_CIRCLE;

			uint uLocalSegID = uSegCircs - uHalfCircPiece*NR_SEGMENTS_ON_HALF_CIRCLE;

			float t = ((float) (uLocalSegID + (uID&0x1))) / ((float) NR_SEGMENTS_ON_HALF_CIRCLE);
			float angle = uHalfCircPiece<2 ? lerp(-halfFov, halfFov, t) : (M_PI*t);


			float cs = cos(angle), si = sin(angle);

			if(uHalfCircPiece==0) { vP = float3(-si, -cs, 0.0); }
			else if(uHalfCircPiece==1) { vP = float3(0.0, -cs, si); }
			else {					   vP = float3(sinHalfFov*cs, -cosHalfFov, sinHalfFov*si); }

			if(uHalfCircPiece==3) vP.xz = -vP.xz;		// rotate half circle by 180 degrees around the Y axis to duplicate
			vP *= radius;
		}

		// transform to view space
		Po = data.vLpos.xyz + mul(vP, float3x3(data.vAxisX, data.vAxisY, data.vAxisZ));
		assignedToPrim_o = true;
	}
}

void GenWedgeVertex(out float3 Po, out bool assignedToPrim_o, VS_INPUT input, SFiniteVolumeData data)
{
	uint uID = input.vert_id;
	if(uID<(2*NR_SEGMENTS_ON_WEDGE))
	{
		float radius = GetVolumeRadius(data);
		float cosHalfFov = data.fPenumbra;
		float sinHalfFov = sqrt(max(0.0, 1-cosHalfFov*cosHalfFov));
		float halfFov = acos(cosHalfFov);
		float segLen = data.fSegLength;

		uint uSegID = uID>>1;

		float3 vP = 0.0;

		if(uSegID<10)
		{
			bool isFirstVert = (uID&0x1)==0;

			
			// rotate by a multiple of 90 degrees around X axis 0, 90, 180 or 270
			vP = radius * float3(0.0, -cosHalfFov, -sinHalfFov);

			bool isMirror = uSegID>=3 && uSegID<6;
			uint uSegIDlocal = isMirror ? (uSegID-3) : uSegID;

			// rotate by a multiple of 90 degrees around Y axis 0, 90, 180 or 270
			vP.zx = (uSegIDlocal&0x1)==0 ? vP.zx : vP.xz;
			vP.z = (((uSegIDlocal+1)>>1)&0x1)!=0 ? -vP.z : vP.z;
			vP.x = (((uSegIDlocal+0)>>1)&0x1)!=0 ? -vP.x : vP.x;

			if(isMirror)
			{
				vP.xz = -vP.xz; vP.x += segLen;
			}

			if(uSegID<6 && isFirstVert) 
			{
				vP = float3(isMirror ? segLen : 0.0, 0.0, 0.0);
			}

			float offs = isFirstVert ? segLen : 0.0; 
			float flip = uSegID==8 ? (-1.0) : 1.0;
			float verticalOffs = uSegID==7 ? (-radius) : 0.0; 
			if(uSegID==6 || uSegID==7) vP = float3(offs, verticalOffs, 0.0);
			else if(uSegID==8 || uSegID==9) vP = float3(offs, -radius*cosHalfFov, flip*radius*sinHalfFov);
		}
		else
		{
			uint uSegHalfCircs = uSegID-10;

			const uint nrCurveSegsOnOneSide = 2*NR_SEGMENTS_ON_HALF_CIRCLE + NR_SEGMENTS_ON_QUARTER_CIRCLE;
			bool isMirror = uSegHalfCircs>=nrCurveSegsOnOneSide;
			uint uSegIDHalfCircsOneHalf = isMirror ? (uSegHalfCircs-nrCurveSegsOnOneSide) : uSegHalfCircs;

			// 0 - Y plane half circle
			// 1 - X plane half circle
			// 2 - Z plane quarter circle
			uint uHalfCircPiece = uSegIDHalfCircsOneHalf / NR_SEGMENTS_ON_HALF_CIRCLE;


			//////////////////
			uint uLocalSegID = uSegIDHalfCircsOneHalf - uHalfCircPiece*NR_SEGMENTS_ON_HALF_CIRCLE;

			float denom = (float) (uHalfCircPiece==2 ? NR_SEGMENTS_ON_QUARTER_CIRCLE : NR_SEGMENTS_ON_HALF_CIRCLE); 
			float t = ((float) (uLocalSegID + (uID&0x1))) / denom;
			float angle = uHalfCircPiece==0 ? (M_PI*t) : lerp(uHalfCircPiece!=2 ? (-halfFov) : 0, halfFov, t);


			float cs = cos(angle), si = sin(angle);

			if(uHalfCircPiece==2) { vP = float3(-si, -cs, 0.0); }
			else if(uHalfCircPiece==1) { vP = float3(0.0, -cs, si); }
			else {					   vP = float3(-sinHalfFov*si, -cosHalfFov, sinHalfFov*cs); }

			vP *= radius;

			if(isMirror) { vP = float3(-vP.x+segLen, vP.y, -vP.z); }
		}

		// transform to view space
		Po = data.vLpos.xyz + mul(vP, float3x3(data.vAxisX, data.vAxisY, data.vAxisZ));
		assignedToPrim_o = true;		
	}
}