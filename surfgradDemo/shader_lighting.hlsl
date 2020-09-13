#include "std_cbuffer.h"
#include "custom_cbuffers.h"
#include "volume_definitions.h"
#include "illum.h"
#include "surfgrad_framework.h"
#include "noise.h"
#include "canvas_common.h"


//-----------------------------------------------------------------------------------------
// Textures and Samplers
//-----------------------------------------------------------------------------------------

Texture2D g_norm_tex;
Texture2D g_norm_os_tex;
Texture2D g_norm_detail_tex;
Texture2D g_albedo_tex;
Texture2D g_smoothness_tex;
Texture2D g_ao_tex;
Texture2D g_mask_tex;
Texture2D g_height_tex;

Texture2D g_decal_norm_tex;
Texture2D g_decal_norm_secondary_tex;
TextureCube g_decal_cube_norm_tex;

// triplanar
Texture2D g_nmapX;
Texture2D g_nmapY;
Texture2D g_nmapZ;

Texture2D g_shadowResolve;
Texture2D g_table_FG;

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
	float3 normal			: TEXCOORD2;
    float4 tangent			: TEXCOORD3;
};


VS_OUTPUT RenderSceneVS( VS_INPUT input)
{
	VS_OUTPUT Output;
	float3 vNormalWorldSpace;

	float3 vP = mul( float4(input.Position.xyz,1.0), g_mLocToWorld ).xyz;
	
	// Transform the position from object space to homogeneous projection space
	Output.Position = mul( float4(vP,1.0), g_mViewProjection );



	// position & normal
	Output.normal = normalize(mul((float3x3) g_mWorldToLocal, input.Normal.xyz));	// inverse transposed for normal
	Output.tangent = float4( normalize(mul(input.Tangent.xyz, (float3x3) g_mLocToWorld)), input.Tangent.w );

	Output.TextureUV = input.TextureUV.xy;
	Output.TextureUV2 = input.TextureUV2.xy;
	
	// flip to upper left origin
	Output.TextureUV = float2(Output.TextureUV.x,1-Output.TextureUV.y); 
	Output.TextureUV2 = float2(Output.TextureUV2.x,1-Output.TextureUV2.y); 
	Output.tangent.w = -Output.tangent.w;

	return Output;    
}


// this function should return true when we observe
// the back-face of a two-sided material
bool IsFlipNormal()
{
	return false;
}


static float3 surfPosInWorld;
static float3 surfPosInView;

void Prologue(VS_OUTPUT In)
{
	// position in camera space
	float4 v4ScrPos = float4(In.Position.xyz, 1);
	float4 v4ViewPos = mul(v4ScrPos, g_mScrToView);
	surfPosInView = v4ViewPos.xyz / v4ViewPos.w;

	// actual world space position
	surfPosInWorld = mul(float4(surfPosInView.xyz,1.0), g_mViewToWorld).xyz;

	// relative world space
	float3 relSurfPos = mul(surfPosInView, (float3x3) g_mViewToWorld);

	// mikkts for conventional vertex-level tangent space
	// (no normalization is mandatory). Using "bitangent on the fly"
	// option in xnormal to reduce vertex shader outputs.
	float sign_w = In.tangent.w > 0.0 ? 1.0 : -1.0;
	mikktsTangent = In.tangent.xyz;
	mikktsBitangent = sign_w*cross(In.normal.xyz, In.tangent.xyz);

	// Prepare for surfgrad formulation w/o breaking mikkTSpace
	// compliance (use same scale as interpolated vertex normal).
	float renormFactor = 1.0/length(In.normal.xyz);
	mikktsTangent   *= renormFactor;
	mikktsBitangent *= renormFactor;
	nrmBaseNormal    = renormFactor*In.normal.xyz;

	// handle two-sided materials. Note that the tangent, bitangent and
	// surface gradients do not flip as a result of flipping the base normal
	if ( IsFlipNormal() ) nrmBaseNormal = -nrmBaseNormal;
	
	// The variables below (plus nrmBaseNormal) need to be
	// recomputed in the case of back-to-back bump mapping.

#if 1
		// NO TRANSLATION! Just 3x3 transform to avoid precision issues.
		dPdx = ddx_fine(relSurfPos);
		dPdy = ddy_fine(relSurfPos);
#else
		// use this path if ddx and ddy are not available options such as during deferred or RTX
		float3 V_c = normalize(-surfPosInView);
		float3 dPdx_c, dPdy_c;
		float3 nrmBaseNormal_c = mul((float3x3) g_mViewToWorld, nrmBaseNormal);	// inverse transposed for normal
		nrmBaseNormal_c = FixNormal(nrmBaseNormal_c, V_c);
		ScreenDerivOfPosNoDDXY(dPdx_c, dPdy_c, surfPosInView, nrmBaseNormal_c, transpose(g_mScrToView), In.Position.x, In.Position.y);
		dPdx = mul(dPdx_c, (float3x3) g_mViewToWorld);		// instead of using g_mScrToView use g_mScrToRelativeWorldSpace
		dPdy = mul(dPdy_c, (float3x3) g_mViewToWorld);		// to skip the additional transformations between world and view space.
#endif

	sigmaX = dPdx - dot(dPdx, nrmBaseNormal)*nrmBaseNormal;
	sigmaY = dPdy - dot(dPdy, nrmBaseNormal)*nrmBaseNormal;
	flip_sign = dot(dPdy, cross(nrmBaseNormal, dPdx)) < 0 ? -1 : 1;

}

float3 Epilogue(VS_OUTPUT In, float3 vN, float3 albedo=pow(float3(72, 72, 72)/255.0,2.2), float smoothness=0.5, float ao=1.0);

float4 SuperSimplePS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);

	float3 albedo=pow(float3(106, 106, 106)/255.0,2.2);
		
	return float4(Epilogue(In, nrmBaseNormal, albedo),1);
}

float3 FetchSignedVector(Texture2D tex, SamplerState samp, float2 st)
{
	return 2*tex.Sample( samp, st ).xyz-1;
}

float3 FetchSignedVectorLevel(Texture2D tex, SamplerState samp, float2 st, float lod)
{
	return 2*tex.SampleLevel( samp, st, lod ).xyz-1;
}

float3 FetchSignedVectorGrad(Texture2D tex, SamplerState samp, float2 st, float2 dSTdx, float2 dSTdy)
{
	return 2*tex.SampleGrad( samp, st, dSTdx, dSTdy ).xyz-1;
}

float3 FetchSignedVectorFromCubeLevel(TextureCube tex, SamplerState samp, float3 Dir, float lod)
{
	return 2*tex.SampleLevel( samp, Dir, lod ).xyz-1;
}

float3 FetchSignedVectorFromCubeGrad(TextureCube tex, SamplerState samp, float3 Dir, float3 dDirdx, float3 dDirdy)
{
	return 2*tex.SampleGrad( samp, Dir, dDirdx, dDirdy ).xyz-1;
}

float2 FetchDeriv(Texture2D tex, SamplerState samp, float2 st)
{
	float3 vec = FetchSignedVector(tex, samp, st);
	return TspaceNormalToDerivative(vec);
}

float2 FetchDerivLevel(Texture2D tex, SamplerState samp, float2 st, float lod)
{
	float3 vec = FetchSignedVectorLevel(tex, samp, st, lod);
	return TspaceNormalToDerivative(vec);
}

float2 FetchDerivGrad(Texture2D tex, SamplerState samp, float2 st, float2 dSTdx, float2 dSTdy)
{
	float3 vec = FetchSignedVectorGrad(tex, samp, st, dSTdx, dSTdy);
	return TspaceNormalToDerivative(vec);
}

static float3 g_sphereAlbedo = pow(float3(77, 45, 32)/255.0,2.2);
//static  float3 g_sphereAlbedo = 1.3*pow(float3(77, 55, 48)/255.0,2.2);

#if defined(BASIC_SAMPLE)
float4 BasicSamplePS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);
	float2 dHduv = FetchDeriv(g_norm_tex, g_samWrap, g_fTileRate*In.TextureUV.xy);
	float3 tang=mikktsTangent, bitang=mikktsBitangent;
	if(!g_bUseVertexTSpace)
	{
		GenBasisTB(tang, bitang, In.TextureUV.xy);	// don't need tile rate
	}
	float3 surfGrad = g_fBumpIntensity * SurfgradFromTBN(dHduv, tang, bitang);
	float3 vN = ResolveNormalFromSurfaceGradient(surfGrad);
	
	float3 albedo = g_sphereAlbedo;
	return float4(Epilogue(In, vN, albedo),1);
}
#endif


#if defined(NMAP_OS)
float4 NMapOSSamplePS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);
	float3 No = FetchSignedVector(g_norm_os_tex, g_samClamp, In.TextureUV.xy);

	// Use inverse transposed. No need to normalize before SurfgradFromPerturbedNormal()
	float3 Nw = mul((float3x3) g_mWorldToLocal, No);
	float3 surfGrad = g_fBumpIntensity * SurfgradFromPerturbedNormal(Nw);
	float3 vN = ResolveNormalFromSurfaceGradient(surfGrad);

	float3 albedo = g_sphereAlbedo;
	return float4(Epilogue(In, vN, albedo),1);
}
#endif

#if defined(SCALE_DEPENDENT)
float4 ScaleDependentSamplePS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);
	float2 texST = g_fTileRate*In.TextureUV.xy;
	float2 dHduv = FetchDeriv(g_norm_tex, g_samWrap, texST);
	uint2 dims;
	g_norm_tex.GetDimensions(dims.x, dims.y);
	float3 surfGrad = g_fBumpIntensity * SurfgradScaleDependent(dHduv,texST, dims);
	float3 vN = ResolveNormalFromSurfaceGradient(surfGrad);
	
	float3 albedo = g_sphereAlbedo;
	return float4(Epilogue(In, vN, albedo),1);
}
#endif

#if defined(MIXING_SAMPLE)
float4 MixingSamplePS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);
	
	// fetch first surface gradient from a base object space normal map
	float3 No = FetchSignedVector(g_norm_os_tex, g_samClamp, In.TextureUV.xy);
	float3 Nw = mul((float3x3) g_mWorldToLocal, No);	// Use inverse transposed. No need to normalize before SurfgradFromPerturbedNormal()
	float3 surfGrad = g_fBaseBumpScale * SurfgradFromPerturbedNormal(Nw);

	// fetch detail derivative
	float2 texST = g_bUseSecondaryUVsForDetails ? In.TextureUV2.xy : In.TextureUV.xy;
	float3 tang=mikktsTangent, bitang=mikktsBitangent;
	if(g_bUseSecondaryUVsForDetails)
	{
		GenBasisTB(tang, bitang, texST);	// don't need tile rate
	}
	float2 dHduv = FetchDeriv(g_norm_detail_tex, g_samWrap, g_fDetailTileRate*texST);
	float3 surfGrad_detail = g_fDetailBumpScale * SurfgradFromTBN(dHduv, tang, bitang);

	// third bump contribution is a procedural 3D function
	float3 samplePos = g_fNoiseTileRate * surfPosInWorld;
	float samplePosPixSize = g_fNoiseTileRate * GetPixelSize(surfPosInWorld);

	float3 volGrad =  dfractalsummax(samplePos, samplePosPixSize).yzw;
	float3 surfGradNoise = g_fNoiseBumpScale * SurfgradFromVolumeGradient(volGrad);


	// we can add, blend and scale surface gradients
	float2 mask = g_mask_tex.Sample(g_samClamp, In.TextureUV.xy);
	surfGrad = lerp(surfGrad + mask.x * surfGrad_detail, surfGradNoise, mask.y);

	// negating a surface gradient corresponds to flipping the height function
	surfGrad = g_bInvertBumpping ? (-surfGrad) : surfGrad;

	// resolve normal
	float3 vN = ResolveNormalFromSurfaceGradient(surfGrad);

	// done
	float3 albedo = g_sphereAlbedo;
	float smoothness = lerp(0.5, 0.43, mask.y);
	return float4(Epilogue(In, vN, albedo, smoothness),1);
}
#endif


#if defined(PLANAR_Y)
float4 PlanarYSamplePS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);
	
	float3 sp = g_fTileRate * surfPosInWorld;

	float2 texST = float2(sp.x, -sp.z);	// since looking at -Z in a right hand coordinate frame.

	// need to negate .y of derivative due to upper-left corner being the texture origin
	texST = float2(texST.x, 1.0-texST.y);
	float2 dHduv = FetchDeriv(g_norm_tex, g_samWrap, texST);
	dHduv.y *= -1.0;		// switch to lower-left origin

	// negate back since we sampled using (x,-z)
	dHduv.y *= -1.0;

	float3 volGrad = float3(dHduv.x, 0.0, dHduv.y);
	float3 surfGrad = SurfgradFromVolumeGradient(volGrad);
	float weightY = DetermineTriplanarWeights(1.0).y;

	surfGrad *= (weightY * g_fBumpIntensity);
	

	float3 vN = ResolveNormalFromSurfaceGradient(surfGrad);
	
	float3 albedo = g_sphereAlbedo;
	return float4(Epilogue(In, vN, albedo),1);
}
#endif

#if defined(TRIPLANAR_WS) || defined(TRIPLANAR_OS) || defined(TRIPLANAR_POST)
float3 CommonTriplanar(float3 position, float3 Nbase)
{
	// backup base normal and patch it
	const float3 recordBaseNorm = nrmBaseNormal;
	nrmBaseNormal = Nbase;


	float3 pos = g_fTileRate * position;

	float2 sp_x = float2(-pos.z, pos.y);
	float2 sp_y = float2(pos.x, -pos.z);
	float2 sp_z = float2(pos.x, pos.y);

	// need to negate .y of derivative due to upper-left corner being the texture origin
	float2 dHduv_x = FetchDeriv(g_nmapX, g_samWrap, float2(sp_x.x, 1.0-sp_x.y));
	float2 dHduv_y = FetchDeriv(g_nmapY, g_samWrap, float2(sp_y.x, 1.0-sp_y.y));
	float2 dHduv_z = FetchDeriv(g_nmapZ, g_samWrap, float2(sp_z.x, 1.0-sp_z.y));
	dHduv_x.y *= -1.0; dHduv_y.y *= -1.0; dHduv_z.y *= -1.0;			// switch to lower-left origin

	// need to negate these back since we used (-z,y) and (x,-z) for sampling
	dHduv_x.x *= -1.0; dHduv_y.y *= -1.0;


	float3 weights = DetermineTriplanarWeights(3.0);
	float3 surfGrad = g_fBumpIntensity * SurfgradFromTriplanarProjection(weights, dHduv_x, dHduv_y, dHduv_z);
	float3 vN = ResolveNormalFromSurfaceGradient(surfGrad);

	// restore base normal 
	nrmBaseNormal = recordBaseNorm;

	return vN;
}
#endif

#if defined(TRIPLANAR_WS)
float4 TriplanarWS_PS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);
	
	float3 vN = CommonTriplanar(surfPosInWorld, nrmBaseNormal);
	
	float3 albedo = g_sphereAlbedo;
	return float4(Epilogue(In, vN, albedo),1);
}
#endif

#if defined(TRIPLANAR_OS)
float4 TriplanarOS_PS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);
	
	float3 nrmBaseNormal_os = normalize(mul((float3x3) g_mLocToWorld, nrmBaseNormal));	// inverse transposed for normal
	float3 surfPos_os = mul(float4(surfPosInWorld.xyz,1.0), g_mWorldToLocal).xyz;

	float3 vN = CommonTriplanar(surfPos_os, nrmBaseNormal_os);
	vN = normalize(mul((float3x3) g_mWorldToLocal, vN));	// inverse transposed for normal

	
	float3 albedo = g_sphereAlbedo;
	return float4(Epilogue(In, vN, albedo),1);
}
#endif

#if defined(TRIPLANAR_POST)
float4 TriplanarPostPS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);

	float2 dHduv = FetchDeriv(g_norm_tex, g_samWrap, g_fBaseTileRate*In.TextureUV.xy);
	float3 surfGrad = g_fBaseBumpScale * SurfgradFromTBN(dHduv, mikktsTangent, mikktsBitangent);
	float3 vNbase = ResolveNormalFromSurfaceGradient(surfGrad);

	// triplanar post resolve
	float3 vN = CommonTriplanar(surfPosInWorld, vNbase);

	nrmBaseNormal = vNbase;			// the interpolated vertex normal is not a good indicator for low poly hard surface geometry. Let's just replace it
	
	float3 albedo = pow(float3(135, 135, 135)/255.0,2.2);
	return float4(Epilogue(In, vN, albedo),1);
}
#endif



#if defined(HARDSURFACE)
float4 HardSurfaceSamplePS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);
	float2 dHduv = FetchDeriv(g_norm_tex, g_samWrap, In.TextureUV.xy);
	
	float3 surfGrad = SurfgradFromTBN(dHduv, mikktsTangent, mikktsBitangent);
	float3 vN = ResolveNormalFromSurfaceGradient(surfGrad);
	
	nrmBaseNormal = vN;			// the interpolated vertex normal is not a good indicator for low poly hard surface geometry. Let's just replace it
	
	float3 albedo=pow(float3(106, 106, 106)/255.0,2.2);
	float smoothness = 0.55;//0.7;
	return float4(Epilogue(In, vN, albedo, smoothness),1);
}
#endif

#if defined(BUMP_FROM_HEIGHTMAP)
float2 SingleTapHeightTodHduv(Texture2D hmap, SamplerState samp, float2 texST)
{
	// only needed in case g_iBumpFromHeightMapMethod==2 to show single tap method
	float height = hmap.Sample(samp, texST);
	float2 dSTdx = ddx(texST), dSTdy = ddy(texST);
	float det = dSTdx.x*dSTdy.y - dSTdx.y*dSTdy.x;

	const float eps = 1.192093e-15F;
	float sgn = det<0.0 ? (-1.0) : 1.0;
	float s = sgn / max(eps, abs(det));

	// use Jacobian as 2x2 inverse of [ ddx(initialST) | ddy(initialST) ]
	float2x2 scrToSTspace = s*float2x2( float2(dSTdy.y, - dSTdy.x), float2(-dSTdx.y, dSTdx.x) );

	float dHdx = ddx_fine(height), dHdy = ddy_fine(height);

	//float dHds = dHdx * dXds + dHdy * dYds;
	//float dHdt = dHdx * dXdt + dHdy * dYdt;
	float2 dHdst = mul(float2(dHdx, dHdy), scrToSTspace);

	uint2 dim; hmap.GetDimensions(dim.x, dim.y);
	float2 dHduv = dHdst / float2(dim.x, dim.y);	

	return dHduv;
}

// not quite the same as the unity sample. This is a scale independent version
float4 BumpFromHeightMapPS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);

	float2 texST = g_fTileRate*In.TextureUV.xy;

	bool useUpscaleHQ = g_iBumpFromHeightMapMethod==0;
	float2 dHduv = DerivFromHeightMap(g_height_tex, g_samWrap,  texST, useUpscaleHQ);

	if(g_iBumpFromHeightMapMethod==2) dHduv = SingleTapHeightTodHduv(g_height_tex, g_samWrap, texST);
	
	float3 surfGrad = g_fBumpIntensity * SurfgradFromTBN(dHduv, mikktsTangent, mikktsBitangent);
	float3 vN = ResolveNormalFromSurfaceGradient(surfGrad);
	
	float3 albedo = g_sphereAlbedo;
	return float4(Epilogue(In, vN, albedo),1);
}
#endif

#if defined(SHOW_TS_FROM_HEIGHTMAP)
float4 ShowTSFromHeightMapPS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);

	int mode = g_iSamplingMethod;		// 0 - upscaleHQ, 1 - 3 tap, 2 - from normal map

	float2 texST = g_fTileRate*In.TextureUV.xy;

	float2 dHduv;
	if(mode!=2)
	{
		// since the normal map was generated offline we don't know the multiplier that
		// was used to generate it from the height map so using a guesstimate for g_fBumpIntensity to match g_height_tex with g_norm_tex 
		bool upscaleHQ = mode==0;
		dHduv = g_fBumpIntensity * DerivFromHeightMap(g_height_tex, g_samWrap,  texST, upscaleHQ);
	}
	else { dHduv = FetchDeriv(g_norm_tex, g_samWrap, texST); }

	
	float3 Nres;
	if(g_bShowNormalsWS)
	{
		float3 surfGrad = SurfgradFromTBN(dHduv, mikktsTangent, mikktsBitangent);
		Nres = ResolveNormalFromSurfaceGradient(surfGrad);
	}
	else
	{
		dHduv.y *= -1.0;		// change origin back to lower-left corner as used in the original image
		// convert to a ts normal
		Nres = normalize(float3(-dHduv.x, -dHduv.y, 1.0));
	}
	
	// use x^2.2 since we are using an sRGB render target for display
	float3 col = pow(saturate(0.5*Nres+0.5), 2.2);
	return float4(col,1);
}
#endif


void FixupPrologueNoDDXY(VS_OUTPUT In)
{
	surfPosInView = mul(float4(surfPosInWorld.xyz,1.0), g_mWorldToView).xyz;
	float3 V_c = normalize(-surfPosInView);
	float3 dPdx_c, dPdy_c;
	float3 nrmBaseNormal_c = mul((float3x3) g_mViewToWorld, nrmBaseNormal);	// inverse transposed for normal
	nrmBaseNormal_c = FixNormal(nrmBaseNormal_c, V_c);
	ScreenDerivOfPosNoDDXY(dPdx_c, dPdy_c, surfPosInView, nrmBaseNormal_c, transpose(g_mScrToView), In.Position.x, In.Position.y);
	dPdx = mul(dPdx_c, (float3x3) g_mViewToWorld);		// instead of using g_mScrToView use g_mScrToRelativeWorldSpace
	dPdy = mul(dPdy_c, (float3x3) g_mViewToWorld);		// to skip the additional transformations between world and view space.

	// 
	sigmaX = dPdx - dot(dPdx, nrmBaseNormal)*nrmBaseNormal;
	sigmaY = dPdy - dot(dPdy, nrmBaseNormal)*nrmBaseNormal;
	flip_sign = dot(dPdy, cross(nrmBaseNormal, dPdx))<0 ? -1 : 1;
}


#if defined(PARALLAX_BASIC) || defined(PARALLAX_DETAILS) || defined(PARALLAX_DENTS)

#define USE_POM_METHOD

float TapHeightCommonLevel(Texture2D hmap, SamplerState samp, float2 texST, float lod)
{
	float height = hmap.SampleLevel(samp, texST, lod).x;
#ifdef USE_POM_METHOD
	// surface of mesh is 0.
	height = height - 1;
#else
	// parallax
	height = height - 0.5;
#endif

	return height;
}

float TapHeightCommon(Texture2D hmap, SamplerState samp, float2 texST)
{
	float height = hmap.Sample(samp, texST).x;
#ifdef USE_POM_METHOD
	// surface of mesh is 0.
	height = height - 1;
#else
	// parallax
	height = height - 0.5;
#endif

	return height;
}

// returns the correction offset vector from  st0
float CorrectIntersectionPoint(float t0, float t1, float ray_h0, float ray_h1, float h0, float h1)
{
	float3 eqR = float3(-(ray_h1-ray_h0), t1-t0, 0.0); eqR.z = -dot(eqR.xy, float2(0.0, ray_h0));		// 0.0 corresponds to t0
	float3 eqT = float3(-(h1-h0), t1-t0, 0.0); eqT.z = -dot(eqT.xy, float2(0.0, h0));					// 0.0 corresponds to t0
	
	const float eps = 1.192093e-15F;
	float determ = eqR.x*eqT.y - eqR.y*eqT.x;
	determ = (determ<0.0 ? -1.0 : 1.0) * max(eps, abs(determ));

	//Ar*t + Br*h  = -Cr
	//At*t + Bt*h  = -Ct

	return saturate( t0 + ((eqT.y*(-eqR.z) + (-eqR.y)*(-eqT.z)) / determ) ); 
}

const float2 RayMarch(Texture2D hmap, SamplerState samp, float2 st0_in, float2 st1_in)
{
	float lod_base = hmap.CalculateLevelOfDetail(samp, st0_in);
	uint2 dims;
	hmap.GetDimensions(dims.x, dims.y);

	float distInPix = length( dims * (st1_in-st0_in) );

	const int iterations = 3;
	float3 st0 = float3(st0_in, 0.0);
	float3 st1 = float3(st1_in, -1.0);


	int nrStepsAlongRay = clamp(4*distInPix, 8.0, 2048);			// very brute-force
	float scale = 1.0 / (float)	nrStepsAlongRay;

	int nrInnerIts = (nrStepsAlongRay+7)/8;

	float t0 = 0.0, t1 = 1.0;
	for(int i=0; i<iterations; i++)
	{
		bool notStopped = true;
		int j=0;

		while(notStopped && j<nrInnerIts)
		{									
			float T1 = lerp(t0, t1, saturate((j*8+1)*scale) );
			float T2 = lerp(t0, t1, saturate((j*8+2)*scale) );
			float T3 = lerp(t0, t1, saturate((j*8+3)*scale) );
			float T4 = lerp(t0, t1, saturate((j*8+4)*scale) );
			float T5 = lerp(t0, t1, saturate((j*8+5)*scale) );
			float T6 = lerp(t0, t1, saturate((j*8+6)*scale) );
			float T7 = lerp(t0, t1, saturate((j*8+7)*scale) );
			float T8 = lerp(t0, t1, saturate((j*8+8)*scale) );

			float h1 = TapHeightCommonLevel(hmap, samp, lerp(st0, st1, T1).xy, lod_base);
			float h2 = TapHeightCommonLevel(hmap, samp, lerp(st0, st1, T2).xy, lod_base);
			float h3 = TapHeightCommonLevel(hmap, samp, lerp(st0, st1, T3).xy, lod_base);
			float h4 = TapHeightCommonLevel(hmap, samp, lerp(st0, st1, T4).xy, lod_base);
			float h5 = TapHeightCommonLevel(hmap, samp, lerp(st0, st1, T5).xy, lod_base);
			float h6 = TapHeightCommonLevel(hmap, samp, lerp(st0, st1, T6).xy, lod_base);
			float h7 = TapHeightCommonLevel(hmap, samp, lerp(st0, st1, T7).xy, lod_base);
			float h8 = TapHeightCommonLevel(hmap, samp, lerp(st0, st1, T8).xy, lod_base);		

		
			float t_s = t0, t_e = t1;
			if(notStopped) { if( lerp(st0, st1, T1).z >= h1 ) { t_s = T1; } else { t_e = T1; notStopped=false; } }
			if(notStopped) { if( lerp(st0, st1, T2).z >= h2 ) { t_s = T2; } else { t_e = T2; notStopped=false; } }
			if(notStopped) { if( lerp(st0, st1, T3).z >= h3 ) { t_s = T3; } else { t_e = T3; notStopped=false; } }
			if(notStopped) { if( lerp(st0, st1, T4).z >= h4 ) { t_s = T4; } else { t_e = T4; notStopped=false; } }
			if(notStopped) { if( lerp(st0, st1, T5).z >= h5 ) { t_s = T5; } else { t_e = T5; notStopped=false; } }
			if(notStopped) { if( lerp(st0, st1, T6).z >= h6 ) { t_s = T6; } else { t_e = T6; notStopped=false; } }
			if(notStopped) { if( lerp(st0, st1, T7).z >= h7 ) { t_s = T7; } else { t_e = T7; notStopped=false; } }
			if(notStopped) { if( lerp(st0, st1, T8).z >= h8 ) { t_s = T8; } else { t_e = T8; notStopped=false; } }

			 t0 = t_s; t1 = t_e;
			 notStopped = notStopped && t0<t1;

			 ++j;
		}

		// update number of taps along ray we allow
		nrStepsAlongRay = clamp(4*distInPix * abs(t1-t0), 8, 16);
		scale = 1.0 / (float) nrStepsAlongRay;
		nrInnerIts = (nrStepsAlongRay+7)/8;

		++i;
	}
	
	float h0 = TapHeightCommonLevel(hmap, samp, lerp(st0, st1, t0).xy, lod_base);
	float h1 = TapHeightCommonLevel(hmap, samp, lerp(st0, st1, t1).xy, lod_base);
	float ray_h0 = lerp(st0, st1, t0).z;
	float ray_h1 = lerp(st0, st1, t1).z;

	float finalT = CorrectIntersectionPoint(t0, t1, ray_h0, ray_h1, h0, h1);
	
	//float finalT = saturate(0.5*(t0+t1));
	return lerp(st0_in, st1_in, finalT).xy - st0_in;
}

void ParallaxCommonBase(out float2 correctedST_o, out float lod_o, VS_OUTPUT In, float2 texST)
{
	float3 V = normalize( mul(-surfPosInView, (float3x3) g_mViewToWorld) );

#ifdef USE_POM_METHOD
	bool skipProj = false;
#else
	bool skipProj = true;
#endif

	// dir: must be a normalized vector in same space as the surface
	// position and normal.
	// bumpScale: p' = p + bumpScale * DisplacementMap(st) * normal
	float2 projV = ProjectVecToTextureSpace(V, texST, g_fBumpScale, skipProj);
#ifdef USE_POM_METHOD
	// Ray march along the line segment from texST to (texST-projV) where texST represents
	// the surface of the mesh at height level 0.0 and (texST-projV) represents level -1.0
	const float2 texCorrectionOffset = RayMarch(g_height_tex, g_samWrap, texST, texST-projV);
#else
	float height = TapHeightCommon(g_height_tex, g_samWrap, texST);
	const float2 texCorrectionOffset = height * projV;
#endif
	
	// use unclamped to allow subsequent modification/bias
	float lod = g_height_tex.CalculateLevelOfDetailUnclamped(g_samWrap, texST);
	float2 texST_corrected = texST + texCorrectionOffset;

#if 0
	float2 dHduv = FetchDerivLevel(g_norm_tex, g_samWrap, texST_corrected, lod);
	const float guessOfflineFactor = 0.01;

	// since the normal map was generated offline we don't know the multiplier that was used to generate
	// it from the height map (unless you kept a record of it?). This is an advantage to computing the
	// derivative straight off the height map as done below.
	float derivBumpScale = guessOfflineFactor * g_fBumpScale;
#else
	float2 dHduv = DerivFromHeightMapLevel(g_height_tex, g_samWrap,  texST_corrected, lod, true);
	float derivBumpScale = g_fBumpScale;	// nice! same factor as for the heights. No guesswork
#endif
	
	// technically SurfgradScaleDependent() should receive texST_corrected
	// but since derivatives are based on ddx and ddy we need to use texST.
	uint2 dims;
	g_height_tex.GetDimensions(dims.x, dims.y);
	float3 surfGrad = derivBumpScale * SurfgradScaleDependent(dHduv,texST, dims);
	float3 vN = ResolveNormalFromSurfaceGradient(surfGrad);

	// correct surface position in case of decals
	float heightNew = TapHeightCommonLevel(g_height_tex, g_samWrap, texST_corrected, lod);
	float3 surfCorrection = TexSpaceOffsToSurface(texST, texCorrectionOffset);

	surfPosInWorld += (surfCorrection + g_fBumpScale * heightNew * nrmBaseNormal);
	nrmBaseNormal = vN;

	FixupPrologueNoDDXY(In);

	// return the corrected texture coordinate and the chosen lod
	correctedST_o = texST_corrected;
	lod_o = lod;
}
#endif

#if defined(PARALLAX_BASIC)
float4 ParallaxBasicPS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);

	float2 texST = g_fTileRate * In.TextureUV.xy;

	float2 correctedST; float lod;	// return these for sampling other textures such as albedo and smoothness.
	ParallaxCommonBase(correctedST, lod, In, texST);	

	return float4(Epilogue(In, nrmBaseNormal),1);
}
#endif

#if defined(PARALLAX_DETAILS)
 float4 ParallaxDetailsPS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);

	float2 texST = g_fTileRate * In.TextureUV.xy;

#ifdef USE_POM_METHOD
	float3 vNorg = nrmBaseNormal;
	float3 V = normalize( -mul(surfPosInView, (float3x3) g_mViewToWorld) );
	float3 surfPosInViewOrg = surfPosInView;
#endif

	float2 correctedST; float lod;	// return these for sampling other textures such as albedo and smoothness.
	ParallaxCommonBase(correctedST, lod, In, texST);
	
	float3 tang, bitang;
	GenBasisTB(tang, bitang, In.TextureUV.xy);	// don't need tile rate

	// g_fTileRate already applied to texST. g_fDetailTileRate is applied on top.
	// Use CalculateLevelOfDetailUnclamped() on the texture coordinate before correction to avoid pixel divergence.
	// We could also simply offset the lod returned by ParallaxCommonBase() using log2(g_fDetailTileRate) but this
	// requires knowing that g_norm_detail_tex and g_height_tex have the same texture resolution.
	float lod_detail = g_norm_detail_tex.CalculateLevelOfDetailUnclamped(g_samWrap, g_fDetailTileRate * texST);

#ifdef USE_POM_METHOD
	float3 vNnew = nrmBaseNormal;
	const float eps = 1.192093e-15F;
	float newNdotV = max( eps, abs(dot(vNnew, V)) );
	float oldNdotV = max( eps, abs(dot(vNorg, V)) );
	float newToOldDistRatio = dot(surfPosInView, surfPosInViewOrg) / dot(surfPosInViewOrg, surfPosInViewOrg);

	// correct for both new distance and new angle on virtual surface
	lod_detail += 0.5*log2(max(exp2(-20), (newToOldDistRatio*newToOldDistRatio*oldNdotV)/newNdotV));	// unproject and reproject
#endif

	float2 dHduv = FetchDerivLevel(g_norm_detail_tex, g_samWrap, g_fDetailTileRate * correctedST.xy, lod_detail);
	float3 surfGrad = SurfgradFromTBN(dHduv, tang, bitang);
	float3 vN = ResolveNormalFromSurfaceGradient(surfGrad);

	return float4(Epilogue(In, vN),1);
}
#endif

#if defined(PARALLAX_DENTS)

// just to match the unity scene which is using a left hand coordinate frame
// the quads in world space are perpendicular to the X axis.
//#define MATCH_UNITY_DENTS_LEFT_HAND_COORDINATE_FRAME


float4 ParallaxDentsPS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);

	float2 texST = g_fTileRate * In.TextureUV.xy;

#ifdef MATCH_UNITY_DENTS_LEFT_HAND_COORDINATE_FRAME
	float3 surfPosInWorld_org = surfPosInWorld;
#endif

	float2 correctedST; float lod;	// return these for sampling other textures such as albedo and smoothness.
	ParallaxCommonBase(correctedST, lod, In, texST);	

	float3 sp = surfPosInWorld;
#ifdef MATCH_UNITY_DENTS_LEFT_HAND_COORDINATE_FRAME
	sp.x = surfPosInWorld_org.x - (surfPosInWorld.x - surfPosInWorld_org.x);
#endif

	float3 samplePos = g_fNoiseTileRate * sp + g_vVolumeBumpOffset;
	float samplePosPixSize = g_fNoiseTileRate * GetPixelSize(sp);
	

	// Use composite function on noise g(f(x,y,z)) = bumpscale * pow( H(q*x,q*y,q*z), k). Here q is the tile rate and k is the Volume Bump Power Value.
	// The derivative of a composite function g(f(x)) is f'(x) * g'(f(x)). So the derivative of our composite function is:
	// q*k*bumpscale * grad(H) * pow(H, k-1)
	float4 v4Result = dturbulence(samplePos, samplePosPixSize);
	float height = 2*v4Result.x-1;		// convert to signed
	float3 volGrad =  2*v4Result.yzw;

	float expVal = max(0.0, g_fVolumeBumpPowerValue-1.0);
	float3 compVolGrad = g_fNoiseTileRate * (expVal+1) * pow( saturate(height), expVal ) * volGrad;

#ifdef MATCH_UNITY_DENTS_LEFT_HAND_COORDINATE_FRAME
	compVolGrad.x = -compVolGrad.x;
#endif

	// resolve
	float3 surfGrad = g_fNoiseBumpScale * SurfgradFromVolumeGradient(compVolGrad);
	float3 vN = ResolveNormalFromSurfaceGradient(surfGrad);

	
	return float4(Epilogue(In, vN),1);
}
#endif


#if defined(BUMP_FROM_FRACTALSUM_NOISE_3D)
float4 BumpFromFractalsumPS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);
	
	float3 samplePos = g_fNoiseTileRate * surfPosInWorld;
	float samplePosPixSize = g_fNoiseTileRate * GetPixelSize(surfPosInWorld);

	float3 volGrad =  dfractalsummax(samplePos, samplePosPixSize).yzw;
	float3 surfGrad = g_fNoiseBumpScale * SurfgradFromVolumeGradient(volGrad);
	float3 vN = ResolveNormalFromSurfaceGradient(surfGrad);
	
	float3 albedo = g_sphereAlbedo;
	return float4(Epilogue(In, vN, albedo),1);
}
#endif

#if defined(BUMP_FROM_TURBULENCE_NOISE_3D)
float4 BumpFromTurbulencePS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);
	
	float3 samplePos = g_fNoiseTileRate * surfPosInWorld;
	float samplePosPixSize = g_fNoiseTileRate * GetPixelSize(surfPosInWorld);

	float3 volGrad =  dturbulencemax(samplePos, samplePosPixSize).yzw;
	float3 surfGrad = g_fNoiseBumpScale * SurfgradFromVolumeGradient(volGrad);
	float3 vN = ResolveNormalFromSurfaceGradient(surfGrad);
	
	float3 albedo = g_sphereAlbedo;
	return float4(Epilogue(In, vN, albedo),1);
}
#endif




#if defined(PIRATE_EXAMPLE)
float4 PirateExamplePS( VS_OUTPUT In ) : SV_TARGET0
{
	Prologue(In);
	float3 mask = g_mask_tex.Sample(g_samClamp, In.TextureUV.xy);
	float2 dHduv = FetchDeriv(g_norm_tex, g_samClamp, In.TextureUV.xy);

	bool useSecUV = g_bUseSecondaryUVsetOnPirate;// || g_bUseSecondaryUVForDetailMap;
	float2 texST_detail = useSecUV ? In.TextureUV2.xy : In.TextureUV.xy;
	float2 dHduv_detail = FetchDeriv(g_norm_detail_tex, g_samWrap, g_fDetailTileRate*texST_detail);
	float3 tang=mikktsTangent, bitang=mikktsBitangent;

	// Use inverse transposed. No need to normalize before SurfgradFromPerturbedNormal()
	float3 No = FetchSignedVector(g_norm_os_tex, g_samClamp, In.TextureUV.xy);
	float3 Nw = mul((float3x3) g_mWorldToLocal, No);

	// g_iTSpaceMode:  0 - vertex tspace,  1 - on the fly tspace, 2 - object space nmap
	float3 surfGrad;
	if(g_iTSpaceMode!=2)
	{
		if(g_iTSpaceMode==1)
		{
			GenBasisTB(tang, bitang, In.TextureUV.xy);	// don't need tile rate
		}
		surfGrad = SurfgradFromTBN(dHduv, tang, bitang);
	}
	else { surfGrad = SurfgradFromPerturbedNormal(Nw); }

	// detail map
	float3 tang_sec, bitang_sec;
	GenBasisTB(tang_sec, bitang_sec, texST_detail.xy);
	float3 surfGrad_detail = g_fDetailBumpScale * SurfgradFromTBN(dHduv_detail, tang_sec, bitang_sec);

	// combine
	surfGrad += mask.x*surfGrad_detail;

	// hair noise
	float factor = 100;		// in the unity scene the fbx is scaled up by 100 for some odd reason so to match 3d noise results we use the same here

	float3 surfPosInLocal = mul(float4(surfPosInWorld.xyz, 1.0), g_mWorldToLocal).xyz / factor;			// want to sample in object space
	float3 samplePos = g_fHairNoiseTileRate * surfPosInLocal;
	float samplePosPixSize = g_fHairNoiseTileRate * GetPixelSize(surfPosInLocal);

	float3 volGradHairDetail =  dfractalsummax(samplePos, samplePosPixSize).yzw;
	volGradHairDetail = factor * mul( volGradHairDetail, (float3x3) g_mLocToWorld );		// transform back into world space

	if(g_bHairNoisePostResolve)
	{
		// resolve what we have so far
		nrmBaseNormal = ResolveNormalFromSurfaceGradient(surfGrad);
		surfGrad = 0;
	}

	float3 surfGradHairDetail = mask.z * g_fHairNoiseBumpScale * SurfgradFromVolumeGradient(volGradHairDetail);
	surfGrad += surfGradHairDetail; 
	
	// resolve
	float3 vN = ResolveNormalFromSurfaceGradient(surfGrad);

	float3 albedo = g_albedo_tex.Sample(g_samClamp, In.TextureUV.xy);
	float smoothness = g_smoothness_tex.Sample(g_samClamp, In.TextureUV.xy).x;
	float ao = g_ao_tex.Sample(g_samClamp, In.TextureUV.xy).x;

	return float4(Epilogue(In, vN, albedo, smoothness, ao),1);
}
#endif


float3 IncomingEnergy(float3 dir)
{
	return GetCanvasColor(dir);
}

// frostbite presentation (moving frostbite to pbr)
float3 GetSpecularDominantDir(float3 vN, float3 vR, float fRealRoughness)
{
    float fInvRealRough = saturate(1 - fRealRoughness);
    float lerpFactor = fInvRealRough * (sqrt(fInvRealRough)+fRealRoughness);

    return lerp(vN, vR, lerpFactor);
}

// marmoset horizon occlusion http://marmosetco.tumblr.com/post/81245981087
float ApproximateSpecularSelfOcclusion(float3 vR, float3 baseNormal)
{
    const float fFadeParam = 1.3;
    float rimmask = clamp( 1 + fFadeParam * dot(vR, baseNormal), 0.0, 1.0);
    rimmask *= rimmask;
    
    return rimmask;
}


float3 ProcessDecals(const int offs, VS_OUTPUT In, float3 vNw);
float3 OverlayHeatMap(float3 res_in, const int offs, const bool skipZeroTiles=false);

float3 Epilogue(VS_OUTPUT In, float3 vN, float3 albedo, float smoothness, float ao)
{
	// find our tile from the pixel coordinate
	uint2 uCoord = (uint2) In.Position.xy;
	uint2 uTile = uCoord / 16;
	int NrTilesX = (g_iWidth+15)/16;
	const int offs = uTile.y*NrTilesX+uTile.x;

	float roughness = (1-smoothness)*(1-smoothness);
	const float eps = 1.192093e-15F;
	//float spow = -2.0 + 2.0/max(eps,roughness*roughness);		// specular power

	float3 vNfinal = vN;
	
#ifdef DECALS_ENABLED
	vNfinal = ProcessDecals(offs, In, vNfinal);
#endif
	
	// do some basic lighting
	float shadow = g_bEnableShadows ? g_shadowResolve[uCoord].x : 1.0;
	float3 vVdir = normalize( mul(-surfPosInView, (float3x3) g_mViewToWorld) );
	float3 vLdir = -g_vSunDir;			  // 31, -30
	const float lightIntensity = 2.5 * M_PI;		// 2.35
	//float3 col = shadow*lightIntensity*float3(1,0.95,0.85)*BRDF2_ts_nphong(vNfinal, nrmBaseNormal, vLdir, vVdir, albedo, float3(1,1,1), spow);
	float3 col = shadow*lightIntensity*float3(1,0.95,0.85)*BRDF2_ts_ggx(vNfinal, nrmBaseNormal, vLdir, vVdir, albedo, float3(1,1,1), smoothness);

	// old school cheesy ambientish effect
	col += albedo*ao*IncomingEnergy(vNfinal);

	if(g_bIndirectSpecular)
	{
		float3 vR = reflect(-vVdir, vNfinal);
		float3 vRspec = GetSpecularDominantDir(vNfinal, vR, roughness);
		float VdotNsat = saturate(dot(vNfinal, vVdir));
		float2 tab_fg = g_table_FG.SampleLevel(g_samClamp, float2(VdotNsat, smoothness), 0.0);
		float specColor = 0.04;
	
		float specularOcc = ApproximateSpecularSelfOcclusion(vRspec, nrmBaseNormal);
		col += specularOcc*(tab_fg.x*specColor + tab_fg.y)*IncomingEnergy(vRspec);
	}

	// overlay optional heat map
	col = OverlayHeatMap(col, offs, true);

	if(g_bShowNormalsWS) col = pow(0.5*vNfinal+0.5, 2.2);

	//col = pow(0.5*(70*dPdy)+0.5, 2.2);

	return col;
}



float4 ExecuteDecalList(const int offs, VS_OUTPUT In, const float3 vVPos, const int decalBlendingMethod=0);

float3 ProcessDecals(const int offs, VS_OUTPUT In, float3 vNw)
{
	float4 res = ExecuteDecalList(offs, In, surfPosInView, g_iDecalBlendingMethod);
	float alpha=res.w;

	float3 vNfinal;

	// if additive or masked using surface gradients
	if(g_iDecalBlendingMethod!=2)
	{
		float3 volGrad=res.xyz;
		float3 surfGrad = SurfgradFromVolumeGradient(volGrad);

		float3 curSurfGrad = SurfgradFromPerturbedNormal(vNw);
		surfGrad += alpha*curSurfGrad;
		vNfinal = ResolveNormalFromSurfaceGradient(surfGrad);
	}
	else
	{
		// use blended decal direction as the new normal
		float3 decalsNormal=res.xyz;
		
		vNfinal = normalize(vNw*alpha + decalsNormal);
	}
	

	return vNfinal;
}


uint FetchIndex(const int offs, uint idx)
{
	return g_vVolumeList[NR_USHORTS_PER_TILE*offs+1+idx];
}

uint GetListVolumeCount(const int offs)
{
	return g_vVolumeList[NR_USHORTS_PER_TILE*offs];
}

float GetCotanHalfFov(float cosHalfFov)
{
	return cosHalfFov * rsqrt(max(FLT_EPSILON, 1.0-cosHalfFov*cosHalfFov));
}

float GetDecalBoxRangeFit(SFiniteVolumeData dclDat)
{
	float3 vSize = dclDat.vBoxInnerDist + (1.0 / dclDat.vBoxInvRange);
	float decalBoxRangeFit = 1.0 / min(vSize.x, vSize.y);
	return decalBoxRangeFit;
}

void CompositeDecal(inout float3 volGrad, inout float alphaOut, float3 curVolGrad, float decalBumpScale, float fAtt, bool doBlend)
{
	if(doBlend)
	{ 
		volGrad = lerp(volGrad,decalBumpScale*curVolGrad,fAtt); 
		alphaOut *= (1-fAtt); 
	}
	else
	{ 
		volGrad+=decalBumpScale*fAtt*curVolGrad;
	}
}


// ray must be the unnormalized vector from the decal to the surface position
float3 GradOfSpotDecal(SFiniteVolumeData dclDat, float3 ray, float3 dPdx_c, float3 dPdy_c)
{
	float tileRate = 5;
	float cosHalfFov = dclDat.fPenumbra;
	float scaleFactor = 0.5*tileRate*GetCotanHalfFov(cosHalfFov);		// todo: need to precompute cotanHalfFov

	// spot volumes are centered at origo but face downward along the -Y axis.
	// For our projection (x/z,y/z) we want to reorient the spot to look down 
	// the positive Z axis in a left hand coordinate frame: (x,y,z) --> (x,-z,-y).
	const float3x3 toDecalST = float3x3(dclDat.vAxisX, -dclDat.vAxisZ, -dclDat.vAxisY);

	float3 v3TexST = mul(toDecalST,ray);

	// use derivative of f/g --> (f'*g - f*g')/g^2
	float denom = max(FLT_EPSILON, v3TexST.z*v3TexST.z);
	float3 gradS = (toDecalST[0]*v3TexST.z - toDecalST[2]*v3TexST.x) / denom;
	float3 gradT = (toDecalST[1]*v3TexST.z - toDecalST[2]*v3TexST.y) / denom;
	float2x3 gradST = float2x3(gradS, gradT);

	float2 texST = scaleFactor*(v3TexST.xy / v3TexST.z) + 0.5;
	float2 dSTdx = scaleFactor*mul(gradST, dPdx_c);
	float2 dSTdy = scaleFactor*mul(gradST, dPdy_c);
			

	dSTdx.y *= -1.0; dSTdy.y *= -1.0;		// decal Y axis is up but texture T axis is down in D3D
	texST = float2(texST.x, 1.0-texST.y);
			
#ifdef DECALS_MIP_MAPPED
	float2 dHduv = FetchDerivGrad(g_decal_norm_tex, g_samWrap, texST, dSTdx, dSTdy);
#else
	float2 dHduv = FetchDerivLevel(g_decal_norm_tex, g_samWrap, texST, 0.0);
#endif
	
	dHduv.y *= -1.0;						// decal Y axis is up but texture T axis is down in D3D

	float3 curVolGrad = dHduv.x*toDecalST[0] + dHduv.y*toDecalST[1];

	return curVolGrad;
}


// For sphere shaped decals a 3D height field H(x,y,z) can be represented by a spherical map such that the function does not
// change with distance to the center of the decal.	The volume gradient of this function will always be tangent to the surface of the sphere
// which means the surface gradient of H(x,y,z) relative to the sphere is also the volume gradient dHdxyz. Thus we can author these bump maps as
// an object space normal map that is stored in a cube map. In the shader we convert to a surface gradient relative to the sphere which also gives us
// the volume gradient of H(x,y,z) which allows us to apply it to an arbitrary receiving surface.
//
// The parameter ray must be the unnormalized vector from the decal to the surface position
float3 GradOfSphericalDecal(SFiniteVolumeData dclDat, float3 ray, float3 dPdx_c, float3 dPdy_c)
{
	// let first, second and third component of normalize(ray) be (A,B,C) then
	// ( A(x,y,z), B(x,y,z), C(x,y,z) ) = (x,y,z) * ((x^2+y^2+z^2)^-0.5)
	// which gives us the following 3 gradient vectors of A, B and C
	//gradA = ((x^2+y^2+z^2)^-0.5) * ( -x*(x,y,z)*(x^2+y^2+z^2)^-1 + (1,0,0) )
	//gradB = ((x^2+y^2+z^2)^-0.5) * ( -y*(x,y,z)*(x^2+y^2+z^2)^-1 + (0,1,0) )
	//gradC = ((x^2+y^2+z^2)^-0.5) * ( -z*(x,y,z)*(x^2+y^2+z^2)^-1 + (0,0,1) )

	//float3 gradRayA = recipLenRay * ( -ray.x*ray*recipLenRaySqr + Vec3(1,0,0) );
	//float3 gradRayB = recipLenRay * ( -ray.y*ray*recipLenRaySqr + Vec3(0,1,0) );
	//float3 gradRayC = recipLenRay * ( -ray.z*ray*recipLenRaySqr + Vec3(0,0,1) );


	const float eps = 1.192093e-15F;
	float recipLenRay = rsqrt(max(eps, dot(ray, ray)));
	ray *= recipLenRay;			// normalize
	float3 gradRayA = recipLenRay * (-ray.x*ray + float3(1,0,0));
	float3 gradRayB = recipLenRay * (-ray.y*ray + float3(0,1,0));
	float3 gradRayC = recipLenRay * (-ray.z*ray + float3(0,0,1));

	float3x3 rayGradMat = float3x3(gradRayA, gradRayB, gradRayC);
	float3 rayDx = mul(rayGradMat, dPdx_c);
	float3 rayDy = mul(rayGradMat, dPdy_c);

	// transform to decal space
	float3x3 rotToDecal = float3x3(dclDat.vAxisX, dclDat.vAxisY, dclDat.vAxisZ);
	float3 ray_ds = mul(rotToDecal, ray);
	float3 rayDx_ds = mul(rotToDecal, rayDx);
	float3 rayDy_ds = mul(rotToDecal, rayDy);
			
#ifdef DECALS_MIP_MAPPED
	float3 dsNorm = normalize( FetchSignedVectorFromCubeGrad(g_decal_cube_norm_tex, g_samWrap, ray_ds, rayDx_ds, rayDy_ds) );
#else
	float3 dsNorm = normalize( FetchSignedVectorFromCubeLevel(g_decal_cube_norm_tex, g_samWrap, ray_ds, 0.0) );
#endif
	const float3 recordBaseNorm = nrmBaseNormal;		// backup base normal
	nrmBaseNormal = ray_ds;
	float3 curVolGrad = SurfgradFromPerturbedNormal( dsNorm );
	nrmBaseNormal = recordBaseNorm;						// restore base normal 
	curVolGrad = mul(curVolGrad, rotToDecal);			// rotate into view space

	return curVolGrad;
}

// ray must be the unnormalized vector from the decal to the surface position
float3 GradOfBoxDecal(SFiniteVolumeData dclDat, float3 ray, float3 dPdx_c, float3 dPdy_c)
{
	float tileRate = 3;
	float scaleFactor = tileRate*0.5*GetDecalBoxRangeFit(dclDat);		// todo: need to precompute decalBoxRangeFit

	float3x3 rotToDecal = float3x3(dclDat.vAxisX, dclDat.vAxisY.xyz, dclDat.vAxisZ);

	float2 texST = scaleFactor*mul(rotToDecal, ray).xy+0.5;
	float2 dSTdx = scaleFactor*mul(rotToDecal, dPdx_c).xy;
	float2 dSTdy = scaleFactor*mul(rotToDecal, dPdy_c).xy;

	dSTdx.y *= -1.0; dSTdy.y *= -1.0;		// decal Y axis is up but texture T axis is down in D3D
	texST = float2(texST.x, 1.0-texST.y);

#ifdef DECALS_MIP_MAPPED
	float2 dHduv = FetchDerivGrad(g_decal_norm_tex, g_samWrap, texST, dSTdx, dSTdy);
#else
	float2 dHduv = FetchDerivLevel(g_decal_norm_tex, g_samWrap, texST, 0.0);
#endif
	dHduv.y *= -1.0;						// decal Y axis is up but texture T axis is down in D3D

	float3 curVolGrad = dHduv.x*rotToDecal[0] + dHduv.y*rotToDecal[1];

	return curVolGrad;
}


float4 ExecuteDecalList(const int offs, VS_OUTPUT In, const float3 vVPos, const int decalBlendingMethod)
{
	float3 volGrad=0;
	float alphaOut=1.0;

	uint uNrDecals = GetListVolumeCount(offs);

	// dPdx and dPdy can be generated using ScreenDerivOfPosNoDDXY() if ddx and ddy
	// are not available options (such as during deferred or RTX rendering). Check the prologue!
	float3 dPdx_c = mul(dPdx, (float3x3) g_mWorldToView);
	float3 dPdy_c = mul(dPdy, (float3x3) g_mWorldToView);
	float3 baseN_c = mul(nrmBaseNormal, (float3x3) g_mWorldToView);

	// settings for g_iDecalBlendingMethod
	// 0 - additive surfgrad, 1 - masked surfgrad, 2 - decal direction as new normal
	// though additive vs. masked is a global setting in this demo it can be set per decal
	bool doBlend = decalBlendingMethod!=0;		// if not set to additive decals

	uint l=0;

	uint uIndex = l<uNrDecals ? FetchIndex(offs, l) : 0;
	uint uVolType = l<uNrDecals ? g_vVolumeData[uIndex].uVolumeType : 0;

	// specialized loop for spot decals
	while(l<uNrDecals && (uVolType==SPOT_CIRCULAR_VOLUME || uVolType==WEDGE_VOLUME))
	{
		SFiniteVolumeData dclDat = g_vVolumeData[uIndex];	
	
		float3 vLp = dclDat.vLpos.xyz;
		float3 Ldir = -dclDat.vAxisY.xyz;
																											
		if(uVolType==WEDGE_VOLUME) vLp += clamp(dot(vVPos-vLp, dclDat.vAxisX.xyz), 0, dclDat.fSegLength) * dclDat.vAxisX.xyz;	// wedge decal

		float3 toDecal  = vLp - vVPos;
		float dist = length(toDecal);
		float3 vL = toDecal / dist;

		float fAttLook = saturate(dist * dclDat.fInvRange + dclDat.fNearRadiusOverRange_LP0);

		float fAngFade = saturate((dclDat.fPenumbra + dot(Ldir.xyz, vL)) * dclDat.fInvUmbraDelta);
		fAngFade = fAngFade*fAngFade*(3-2*fAngFade);    // smooth curve
		fAngFade *= fAngFade;                           // apply an additional squaring curve
		fAttLook *= fAngFade;                           // finally apply this to the dist att.

		float3 cenProjPos = dclDat.vLpos.xyz;
		if(uVolType==WEDGE_VOLUME) cenProjPos += 0.5*dclDat.fSegLength * dclDat.vAxisX.xyz;
		float3 ray  = vVPos - cenProjPos;
			
		float3 curVolGrad = GradOfSpotDecal(dclDat, ray, dPdx_c, dPdy_c);

		float decalBumpScale = doBlend ? 1.3 : 0.7;	// arb choice

		// resolve early if we are perturbing decal projected direction as new normal
		// spots are aligned along -Yaxis so new normal is Yaxis
		if(decalBlendingMethod==2)
		{
			curVolGrad = normalize(dclDat.vAxisY - decalBumpScale*curVolGrad);
			decalBumpScale = 1.0;
		}

		CompositeDecal(volGrad, alphaOut, curVolGrad, decalBumpScale, fAttLook, doBlend);
			

		++l; uIndex = l<uNrDecals ? FetchIndex(offs, l) : 0;
		uVolType = l<uNrDecals ? g_vVolumeData[uIndex].uVolumeType : 0;
	}
		
	// specialized loop for sphere decals
	while(l<uNrDecals && (uVolType==SPHERE_VOLUME || uVolType==CAPSULE_VOLUME))
	{
		SFiniteVolumeData dclDat = g_vVolumeData[uIndex];	
		float tileRate = 5;

		float3 vLp = dclDat.vLpos.xyz;

		if(uVolType==CAPSULE_VOLUME) vLp += clamp(dot(vVPos-vLp, dclDat.vAxisX.xyz), 0, dclDat.fSegLength) * dclDat.vAxisX.xyz;		// capsule decal

		float3 toSurfP = vVPos-vLp;
		float dist = length(toSurfP);

		float fAttLook = saturate(dist * dclDat.fInvRange + dclDat.fNearRadiusOverRange_LP0);

		float3 cenProjPos = dclDat.vLpos.xyz;
		if(uVolType==CAPSULE_VOLUME) cenProjPos += 0.5*dclDat.fSegLength * dclDat.vAxisX.xyz;
		float3 ray  = vVPos - cenProjPos;

		float3 curVolGrad = GradOfSphericalDecal(dclDat, ray, dPdx_c, dPdy_c);

		float angularFade = pow(saturate(dot(baseN_c, -ray)), 0.2);//saturate(4*dot(baseN_c, -ray));
		fAttLook *=  angularFade;
		float decalBumpScale = 0.6;// * angularFade;

		// resolve early if we are perturbing decal projected direction as new normal
		if(decalBlendingMethod==2) 
		{
			curVolGrad = normalize(normalize(-ray) - decalBumpScale*curVolGrad);
			decalBumpScale = 1.0;
		}
			
		CompositeDecal(volGrad, alphaOut, curVolGrad, decalBumpScale, fAttLook, doBlend);


		++l; uIndex = l<uNrDecals ? FetchIndex(offs, l) : 0;
		uVolType = l<uNrDecals ? g_vVolumeData[uIndex].uVolumeType : 0;
	}

	// specialized loop for box decals
	while(l<uNrDecals && uVolType==BOX_VOLUME)
	{
		// box volumes are centered at origo and face along the negative Z axis in a right hand coordinate frame.
		SFiniteVolumeData dclDat = g_vVolumeData[uIndex];	
		
		float3 ray  = vVPos - dclDat.vLpos.xyz;

		// smooth fall off
		float3x3 rotToDecal = float3x3(dclDat.vAxisX, dclDat.vAxisY.xyz, dclDat.vAxisZ);
		float3 Pds = mul(rotToDecal, ray);
		float3 dist = saturate( (abs(Pds)-dclDat.vBoxInnerDist) * dclDat.vBoxInvRange );
			
		float fAttLook = max(max(dist.x, dist.y), dist.z);
		fAttLook = 1-fAttLook;
		fAttLook = fAttLook*fAttLook*(3-2*fAttLook);

		float3 curVolGrad = GradOfBoxDecal(dclDat, ray, dPdx_c, dPdy_c);
		float decalBumpScale = doBlend ? 1.3 : 0.7;	// arb choice

		// resolve early if we are perturbing decal projected direction as new normal
		// boxes project along -Zaxis so new normal is +Zaxis
		if(decalBlendingMethod==2) 
		{
			curVolGrad = normalize(dclDat.vAxisZ - decalBumpScale*curVolGrad);
			decalBumpScale = 1.0;
		}
	
		CompositeDecal(volGrad, alphaOut, curVolGrad, decalBumpScale, fAttLook, doBlend);

		++l; uIndex = l<uNrDecals ? FetchIndex(offs, l) : 0;
		uVolType = l<uNrDecals ? g_vVolumeData[uIndex].uVolumeType : 0;
	}


	// need to return the final volume gradient in world space
	volGrad = mul(volGrad, (float3x3) g_mViewToWorld);

	return float4(volGrad, alphaOut);
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

		uint uNrDecals = GetListVolumeCount(offs);

		float fMaxNrDecalsPerTile = 24;
		
		if(!skipZeroTiles || uNrDecals>0)
		{
			// change of base
			// logb(x) = log2(x) / log2(b)
			int nColorIndex = uNrDecals==0 ? 0 : (1 + (int) floor(10 * (log2((float)uNrDecals) / log2(fMaxNrDecalsPerTile))) );
			nColorIndex = nColorIndex<0 ? 0 : nColorIndex;
			float4 col = nColorIndex>11 ? float4(1.0,1.0,1.0,1.0) : kRadarColors[nColorIndex];
			col.xyz = pow(col.xyz, 2.2);

			res = uNrDecals==0 ? 0 : (res*(1-col.w) + 0.45*col*col.w);
		}
	}

	return res;
}











/*
float3 GetWorleyJitter(float3 P)
{		 
	int3 Pi = (int3) floor(P);
	uint seed = 702395077*Pi.x + 915488749*Pi.y + 2120969693*Pi.z;

	seed=1402024253*seed+586950981; // churn

	// compute the 0..1 feature point location's XYZ
	const float fx=(seed+0.5)*(1.0/4294967296.0); 
	seed=1402024253*seed+586950981; // churn
	const float fy=(seed+0.5)*(1.0/4294967296.0);
	seed=1402024253*seed+586950981; // churn
	const float fz=(seed+0.5)*(1.0/4294967296.0);
	const float3 jitter = float3(fx, fy, fz);

	return jitter;
}

float3 GradOfFunkyDots(float3 P, float fPixSize_in=1, float shrink=0.7)
{
	float recipShrink = 1.0/shrink;
	float pixSize = 2*recipShrink*VerifyPixSize(fPixSize_in);

	float3 Pfrac = frac(P);
	float3 Psigned = recipShrink*(2*Pfrac-1);

	Psigned += (1-(shrink+0.05))*(2*GetWorleyJitter(P)-1);

			
	// H(x) = 1 + cos(x*x*pi) where x represents distance to center
	float pi = 3.1415926535897932384626433832795;
	float derivOfPsigned = 2*recipShrink;
	float distSqr = dot(Psigned,Psigned);
	// H(P) = 1+cos(dot(Psigned,Psigned)*pi);
	float3 gradH = distSqr>1.0 ? 0.0 : (2*pi*derivOfPsigned*Psigned * -sin(distSqr*pi));	// gradient(H(P))
				
	gradH *= (1 - smoothstep(0.25,0.75,pixSize));

	return gradH;
}

			float3 P = tileRate*surfPosInWorld;
			float pixSize = tileRate*wsPixSize;			// float wsPixSize = GetPixelSize(surfPosInWorld);
			float3 curVolGrad = tileRate*GradOfFunkyDots(P, pixSize);
			
			// since we accumulate box and spot decals in view space we need to transform the gradient into view space before accumulation		
			curVolGrad = mul(curVolGrad, (float3x3) g_mWorldToView);

			float decalBumpScale = doBlend ? (0.3*0.1) : 0.5*(0.3*0.1);
*/