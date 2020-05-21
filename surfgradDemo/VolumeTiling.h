#ifndef __LIGHTTILING_H__
#define __LIGHTTILING_H__

struct SFiniteVolumeData;
struct SFiniteVolumeBound;

#include <geommath/geommath.h>


class CVolumeTiler
{
public:
	void InitFrame(const Mat44 &mWorldToCam, const Mat44 &mProjection);
	void InitTiler();
	void CompileVolumeList();
	void AddVolume(const SFiniteVolumeData &volumeData, const SFiniteVolumeBound &coliData);
	const SFiniteVolumeBound * GetOrderedBoundsList() const { return m_pVolumeColiDataOrdered; }
	const SFiniteVolumeData * GetVolumesDataList() const { return m_pVolumeDataOrdered; }
	const Vec3 * GetScrBoundsList() const { return m_pScrBounds; }


	CVolumeTiler();
	~CVolumeTiler();

private:
	int m_iIndex, m_iNrVisibVolumes;

	SFiniteVolumeData * m_pVolumeDataScattered;
	SFiniteVolumeData * m_pVolumeDataOrdered;

	SFiniteVolumeBound * m_pVolumeColiDataScattered;
	SFiniteVolumeBound * m_pVolumeColiDataOrdered;

	Vec3 * m_pScrBounds;

	Mat44 m_mWorldToCam;
	Mat44 m_mProjection;
};


#endif