#include "VolumeTiling.h"
#include <geommath/geommath.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <float.h>
#include "volume_definitions.h"


void CVolumeTiler::AddVolume(const SFiniteVolumeData &volumeData, const SFiniteVolumeBound &coliData)
{
	Mat33 m33WrldToCam;
	for(int r=0; r<3; r++)
	{
		Vec4 row = GetRow(m_mWorldToCam, r);
		SetRow(&m33WrldToCam, r, Vec3(row.x, row.y, row.z));
	}

	m_pVolumeDataScattered[m_iIndex] = volumeData;
	m_pVolumeColiDataScattered[m_iIndex] = coliData;

	m_pVolumeDataScattered[m_iIndex].vLpos = m_mWorldToCam*volumeData.vLpos;
	m_pVolumeDataScattered[m_iIndex].vAxisX = m33WrldToCam*volumeData.vAxisX;
	m_pVolumeDataScattered[m_iIndex].vAxisY = m33WrldToCam*volumeData.vAxisY;
	m_pVolumeDataScattered[m_iIndex].vAxisZ = m33WrldToCam*volumeData.vAxisZ;

	const Vec3 c0 = coliData.vBoxAxisX;
	const Vec3 c1 = coliData.vBoxAxisY;
	const Vec3 c2 = coliData.vBoxAxisZ;

	m_pVolumeColiDataScattered[m_iIndex].vBoxAxisX = m33WrldToCam*c0;
	m_pVolumeColiDataScattered[m_iIndex].vBoxAxisY = m33WrldToCam*c1;
	m_pVolumeColiDataScattered[m_iIndex].vBoxAxisZ = m33WrldToCam*c2;

	const Vec3 vCen = m_mWorldToCam*coliData.vCen;
	m_pVolumeColiDataScattered[m_iIndex].vCen = vCen;
	

	++m_iIndex;
}


void CVolumeTiler::InitTiler()
{
	m_pVolumeDataScattered = new SFiniteVolumeData[MAX_NR_VOLUMES_PER_CAMERA];
	m_pVolumeDataOrdered = new SFiniteVolumeData[MAX_NR_VOLUMES_PER_CAMERA];

	m_pVolumeColiDataScattered = new SFiniteVolumeBound[MAX_NR_VOLUMES_PER_CAMERA];
	m_pVolumeColiDataOrdered = new SFiniteVolumeBound[MAX_NR_VOLUMES_PER_CAMERA];

	m_pScrBounds = new Vec3[MAX_NR_VOLUMES_PER_CAMERA*2];
}

// sort by type in linear time
void CVolumeTiler::CompileVolumeList()
{
	m_iNrVisibVolumes = m_iIndex;

	int numOfType[MAX_TYPES], lgtOffs[MAX_TYPES], curIndex[MAX_TYPES];
	for(int i=0; i<MAX_TYPES; i++) { numOfType[i]=0; curIndex[i]=0; }

	// determine number of lights of each type
	for(int l=0; l<m_iNrVisibVolumes; l++) 
	{
		const int iTyp = m_pVolumeDataScattered[l].uVolumeType;
		assert(iTyp>=0 && iTyp<MAX_TYPES);
		++numOfType[iTyp];
	}

	// determine offset for each type
	lgtOffs[0]=0;
	for(int i=1; i<MAX_TYPES; i++) lgtOffs[i]=lgtOffs[i-1]+numOfType[i-1];
	
	// sort lights by type
	for(int l=0; l<m_iNrVisibVolumes; l++) 
	{
		const int iTyp = m_pVolumeDataScattered[l].uVolumeType;
		assert(iTyp>=0 && iTyp<MAX_TYPES);
		
		const int offs = curIndex[iTyp]+lgtOffs[iTyp];
		m_pVolumeDataOrdered[offs] = m_pVolumeDataScattered[l];
		m_pVolumeColiDataOrdered[offs] = m_pVolumeColiDataScattered[l];
		
		++curIndex[iTyp];
	}

	for(int i=0; i<MAX_TYPES; i++) assert(curIndex[i]==numOfType[i]);		// sanity check
}


void CVolumeTiler::InitFrame(const Mat44 &mWorldToCam, const Mat44 &mProjection)
{
	m_mWorldToCam = mWorldToCam;
	m_mProjection = mProjection;

	m_iNrVisibVolumes = 0;
	m_iIndex = 0;
}



CVolumeTiler::CVolumeTiler()
{
	m_pVolumeDataScattered = NULL;
	m_pVolumeDataOrdered = NULL;

	m_pVolumeColiDataScattered = NULL;
	m_pVolumeColiDataOrdered = NULL;

	m_pScrBounds = NULL;

	m_iIndex = 0;
	m_iNrVisibVolumes = 0;
}

CVolumeTiler::~CVolumeTiler()
{
	delete [] m_pVolumeDataScattered;
	delete [] m_pVolumeDataOrdered;

	delete [] m_pVolumeColiDataScattered;
	delete [] m_pVolumeColiDataOrdered;

	delete [] m_pScrBounds;
}