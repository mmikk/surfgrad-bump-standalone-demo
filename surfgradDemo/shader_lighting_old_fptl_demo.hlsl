#include "std_cbuffer.h"
#include "volume_definitions.h"
#include "illum.h"


//-----------------------------------------------------------------------------------------
// Textures and Samplers
//-----------------------------------------------------------------------------------------

Texture2D g_norm_tex;


Buffer<uint> g_vVolumeList;
StructuredBuffer<SFiniteVolumeData> g_vVolumeData;

SamplerState g_samWrap;
SamplerState g_samClamp;
SamplerComparisonState g_samShadow;

//--------------------------------------------------------------------------------------
// shader input/output structure
//--------------------------------------------------------------------------------------
struct VS_INPUT
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float2 TextureUV    : TEXCOORD0;
	float2 TextureUV2   : TEXCOORD1;
    float4 Tangent		: TEXCOORD2;
    //float4 BiTangent    : TEXCOORD2;
};

struct VS_OUTPUT
{
    float4 Position     : SV_POSITION;
    float4 Diffuse      : COLOR0;
    float2 TextureUV    : TEXCOORD0;
	float2 TextureUV2   : TEXCOORD1;
	float3 norm			: TEXCOORD2;
    float4 tang			: TEXCOORD3;
};


VS_OUTPUT RenderSceneVS( VS_INPUT input)
{
	VS_OUTPUT Output;
	float3 vNormalWorldSpace;

	float3 vP = mul( float4(input.Position.xyz,1.0), g_mLocToWorld ).xyz;
	
	// Transform the position from object space to homogeneous projection space
	Output.Position = mul( float4(vP,1.0), g_mViewProjection );



	// position & normal
	Output.norm = normalize(mul((float3x3) g_mWorldToLocal, input.Normal.xyz));	// inverse transposed for normal
	Output.tang = float4( normalize(mul(input.Tangent.xyz, (float3x3) g_mLocToWorld)), input.Tangent.w );

	Output.TextureUV = input.TextureUV.xy;
	Output.TextureUV2 = input.TextureUV2.xy;
	
	// flip to upper left origin
	Output.TextureUV = float2(Output.TextureUV.x,1-Output.TextureUV.y); 
	Output.TextureUV2 = float2(Output.TextureUV2.x,1-Output.TextureUV2.y); 
	Output.tang.w = -Output.tang.w;

	return Output;    
}






float3 ProcessVolume(float3 res_in, VS_OUTPUT In, float3 vNw, float3 vBaseNw);
float3 ExecuteLightList(const int offs, const float3 vVPos, float3 vNw, float3 vBaseNw);
float3 OverlayHeatMap(float3 res_in, const int offs, const bool skipZeroTiles=false);

uint GetListVolumeCount(const int offs)
{
	return g_vVolumeList[NR_USHORTS_PER_TILE*offs];
}

uint FetchIndex(const int offs, uint idx)
{
	return g_vVolumeList[NR_USHORTS_PER_TILE*offs+1+idx];
}


float4 RenderScenePS( VS_OUTPUT In ) : SV_TARGET0
{ 	
	// mikkts for conventional vertex level tspace (no normalizes
	// is mandatory).
	float signw = In.tang.w>0 ? 1.0 : (-1.0);
	float3 tang = In.tang.xyz;
	float3 bitang = signw * cross(In.norm.xyz, In.tang.xyz);
	float3 norm = In.norm.xyz;

	//
	float3 vN = normalize(In.norm.xyz);

	// execute lighting
	float3 res = 0;
	res = ProcessVolume(res, In, vN, vN);

	return float4(res,1);
}

float3 ProcessVolume(float3 res_in, VS_OUTPUT In, float3 vNw, float3 vBaseNw)
{
		// position in camera space
	float4 v4ScrPos = float4(In.Position.xyz, 1);
	float4 v4ViewPos = mul(v4ScrPos, g_mScrToView);
	float3 surfPosInView = v4ViewPos.xyz / v4ViewPos.w;


	// find our tile from the pixel coordinate (tiled forward lighting)
	uint2 uTile = ((uint2) In.Position.xy) / 16;
	int NrTilesX = (g_iWidth+15)/16;
	const int offs = uTile.y*NrTilesX+uTile.x;
	
	float3 res = res_in + ExecuteLightList( offs, surfPosInView, vNw, vBaseNw );

	res = OverlayHeatMap(res, offs);

	return res;
}

float3 OverlayHeatMap(float3 res_in, const int offs, const bool skipZeroTiles)
{
	float3 res = res_in;

	// debug heat vision
	if(g_iMode == 0)		// debug view on
	{
		const float4 kRadarColors[12] = 
		{
			float4(0.0,0.0,0.0,0.0),   // black
			float4(0.0,0.0,0.6,0.5),   // dark blue
			float4(0.0,0.0,0.9,0.5),   // blue
			float4(0.0,0.6,0.9,0.5),   // light blue
			float4(0.0,0.9,0.9,0.5),   // cyan
			float4(0.0,0.9,0.6,0.5),   // blueish green
			float4(0.0,0.9,0.0,0.5),   // green
			float4(0.6,0.9,0.0,0.5),   // yellowish green
			float4(0.9,0.9,0.0,0.5),   // yellow
			float4(0.9,0.6,0.0,0.5),   // orange
			float4(0.9,0.0,0.0,0.5),   // red
			float4(1.0,0.0,0.0,0.9)    // strong red
		};

		uint uNrLights = GetListVolumeCount(offs);

		float fMaxNrLightsPerTile = 24;
		
		if(!skipZeroTiles || uNrLights>0)
		{
			// change of base
			// logb(x) = log2(x) / log2(b)
			int nColorIndex = uNrLights==0 ? 0 : (1 + (int) floor(10 * (log2((float)uNrLights) / log2(fMaxNrLightsPerTile))) );
			nColorIndex = nColorIndex<0 ? 0 : nColorIndex;
			float4 col = nColorIndex>11 ? float4(1.0,1.0,1.0,1.0) : kRadarColors[nColorIndex];
			col.xyz = pow(col.xyz, 2.2);

			res = uNrLights==0 ? 0 : (res*(1-col.w) + 0.45*col*col.w);
		}
	}

	return res;
}



float3 ExecuteLightList(const int offs, const float3 vVPos, float3 vNw, float3 vBaseNw)
{
	float3 ints = 0;

	float3 vVw = normalize( mul(-vVPos, (float3x3) g_mViewToWorld) );

	// hack since we're not actually using imported values for this demo
	float3 vMatColDiff = 0.26*float3(1,1,1);
	float3 vMatColSpec = 0.5*0.3;
	float fSpecPow = 12;



	uint uNrLights = GetListVolumeCount(offs);

	uint l=0;


	uint uIndex = l<uNrLights ? FetchIndex(offs, l) : 0;
	uint uLgtType = l<uNrLights ? g_vVolumeData[uIndex].uVolumeType : 0;

	// specialized loop for spot lights
	while(l<uNrLights && (uLgtType==SPOT_CIRCULAR_VOLUME || uLgtType==WEDGE_VOLUME))
	{
		SFiniteVolumeData lgtDat = g_vVolumeData[uIndex];	
		float3 Ldir = -lgtDat.vAxisY.xyz;
		float3 vLp = lgtDat.vLpos.xyz;
																											
		if(uLgtType==WEDGE_VOLUME) vLp += clamp(dot(vVPos-vLp, lgtDat.vAxisX.xyz), 0, lgtDat.fSegLength) * lgtDat.vAxisX.xyz;

		float3 toLight  = vLp - vVPos;
		float dist = length(toLight);

		float3 vL = toLight / dist;
		float3 vLw = mul(vL, (float3x3) g_mViewToWorld);
		float fAttLook = saturate(dist * lgtDat.fInvRange + lgtDat.fNearRadiusOverRange_LP0);

			
		float fAngFade = saturate((lgtDat.fPenumbra + dot(Ldir.xyz, vL)) * lgtDat.fInvUmbraDelta);	// nuts but entry vBoxAxisX is the spot light dir
		fAngFade = fAngFade*fAngFade*(3-2*fAngFade);    // smooth curve
		fAngFade *= fAngFade;                           // apply an additional squaring curve
		fAttLook *= fAngFade;                           // finally apply this to the dist att.

		ints += lgtDat.vCol*fAttLook*BRDF2_ts_nphong_nofr(vNw, vBaseNw, vLw, vVw, vMatColDiff, vMatColSpec, fSpecPow);


		++l; uIndex = l<uNrLights ? FetchIndex(offs, l) : 0;
		uLgtType = l<uNrLights ? g_vVolumeData[uIndex].uVolumeType : 0;
	}
		
	// specialized loop for sphere lights
	while(l<uNrLights && (uLgtType==SPHERE_VOLUME || uLgtType==CAPSULE_VOLUME))
	{
		SFiniteVolumeData lgtDat = g_vVolumeData[uIndex];	
		float3 vLp = lgtDat.vLpos.xyz;

		if(uLgtType==CAPSULE_VOLUME) vLp += clamp(dot(vVPos-vLp, lgtDat.vAxisX.xyz), 0, lgtDat.fSegLength) * lgtDat.vAxisX.xyz;		// capsule light

		float3 toLight  = vLp - vVPos; 
		float dist = length(toLight);

		float3 vL = toLight / dist;
		float3 vLw = mul(vL, (float3x3) g_mViewToWorld);
		float fAttLook = saturate(dist * lgtDat.fInvRange + lgtDat.fNearRadiusOverRange_LP0);

		ints += lgtDat.vCol*fAttLook*BRDF2_ts_nphong_nofr(vNw, vBaseNw, vLw, vVw, vMatColDiff, vMatColSpec, fSpecPow);


		++l; uIndex = l<uNrLights ? FetchIndex(offs, l) : 0;
		uLgtType = l<uNrLights ? g_vVolumeData[uIndex].uVolumeType : 0;
	}

	// specialized loop for box lights
	while(l<uNrLights && uLgtType==BOX_VOLUME)
	{
		SFiniteVolumeData lgtDat = g_vVolumeData[uIndex];	
		
		//float3 vBoxAxisY = lgtDat.vLdir.xyz;
		float3 toLight  = lgtDat.vLpos.xyz - vVPos;

		float3 dist = float3( dot(toLight, lgtDat.vAxisX), dot(toLight, lgtDat.vAxisY), dot(toLight, lgtDat.vAxisZ) );
		dist = abs(dist) - lgtDat.vBoxInnerDist;
		dist = saturate(dist * lgtDat.vBoxInvRange);

		float3 vL = normalize(toLight);
		float3 vLw = mul(vL, (float3x3) g_mViewToWorld);
		float fAttLook = max(max(dist.x, dist.y), dist.z);

		// arb ramp
		fAttLook = 1-fAttLook;
		fAttLook = fAttLook*fAttLook*(3-2*fAttLook);
			

		ints += lgtDat.vCol*fAttLook*BRDF2_ts_nphong_nofr(vNw, vBaseNw, vLw, vVw, vMatColDiff, vMatColSpec, fSpecPow);


		++l; uIndex = l<uNrLights ? FetchIndex(offs, l) : 0;
		uLgtType = l<uNrLights ? g_vVolumeData[uIndex].uVolumeType : 0;
	}

	return ints;
}