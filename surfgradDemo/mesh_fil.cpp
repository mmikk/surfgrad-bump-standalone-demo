#include "mesh_fil.h"
#include "quadtree.h"
#include <stdio.h>


struct SFilVertX
{
	Vec3 vert;	// position
	float s, t;	// texcoord

	Vec3 vOs; float fMagS;	// first order derivative with respect to, s
	Vec3 vOt; float fMagT;	// first order derivative with respect to, t
	Vec3 norm;				// normal
};


bool CMeshFil::ReadMeshFil(ID3D11Device* pd3dDev, const char file_name[], const float fScale, const bool bCenter, const bool bGenQuadTree)
{
	// clean up previous
	CleanUp();


	bool bSuccess = false;
	FILE * fptr = fopen(file_name, "rb");
	if(fptr!=NULL)
	{
		fread((void *) &m_iNrVerts, sizeof(int), 1, fptr);
		fread((void *)&m_iNrFaces, sizeof(int), 1, fptr);

		SFilVertX * tmpVerts = new SFilVertX[m_iNrVerts];
		if(tmpVerts!=NULL)
		{
			fread(tmpVerts, sizeof(SFilVertX), m_iNrVerts, fptr);

			m_vVerts = new SFilVert[m_iNrVerts];
			m_iIndices = new int[3*m_iNrFaces];

			if(m_vVerts!=NULL && m_iIndices!=NULL)
			{
				bSuccess=true;

				
				fread(m_iIndices, sizeof(int), 3*m_iNrFaces, fptr);
				fclose(fptr);

				// convert
				for(int i=0; i<m_iNrVerts; i++)
				{
					m_vVerts[i].pos = tmpVerts[i].vert;
					m_vVerts[i].norm = tmpVerts[i].norm;
					m_vVerts[i].s = tmpVerts[i].s; m_vVerts[i].t = tmpVerts[i].t;
					m_vVerts[i].s2 = 0.0f; m_vVerts[i].t2 = 0.0f;
					const Vec3 vT = tmpVerts[i].vOs;
					m_vVerts[i].tang = Vec4(vT.x, vT.y, vT.z, (tmpVerts[i].vOs * Cross(tmpVerts[i].norm, vT))<0.0f ? (-1.0f) : 1.0f);
				}

				// flip faces
				for(int q=0; q<m_iNrFaces; q++)
				{
					int index = m_iIndices[q*3+1];
					m_iIndices[q*3+1] = m_iIndices[q*3+2];
					m_iIndices[q*3+2] = index;
				}

				// scale and center
				Vec3 vMin=m_vVerts[0].pos * fScale;
				Vec3 vMax=vMin;
				for(int k=0; k<m_iNrVerts; k++)
				{
					m_vVerts[k].pos *= fScale;
					if(vMin.x>m_vVerts[k].pos.x) vMin.x=m_vVerts[k].pos.x;
					else if(vMax.x<m_vVerts[k].pos.x) vMax.x=m_vVerts[k].pos.x;
					if(vMin.y>m_vVerts[k].pos.y) vMin.y=m_vVerts[k].pos.y;
					else if(vMax.y<m_vVerts[k].pos.y) vMax.y=m_vVerts[k].pos.y;
					if(vMin.z>m_vVerts[k].pos.z) vMin.z=m_vVerts[k].pos.z;
					else if(vMax.z<m_vVerts[k].pos.z) vMax.z=m_vVerts[k].pos.z;
				}
				const Vec3 vCen = 0.5f*(vMax+vMin);

				if(bCenter)
				{
					for(int k=0; k<m_iNrVerts; k++)
					{
						m_vVerts[k].pos -= vCen;
					}
					vMin -= vCen; vMax -= vCen;
				}
				m_vMin = vMin; m_vMax = vMax;

				HRESULT hr;

				// Set initial data info
				D3D11_SUBRESOURCE_DATA InitData;
				InitData.pSysMem = m_vVerts;

				// Fill DX11 vertex buffer description
				D3D11_BUFFER_DESC     bd;
				bd.Usage =            D3D11_USAGE_DEFAULT;
				bd.ByteWidth =        sizeof( SFilVert ) * m_iNrVerts;
				bd.BindFlags =        D3D11_BIND_VERTEX_BUFFER;
				bd.CPUAccessFlags =   0;
				bd.MiscFlags =        0;

				// Create DX11 vertex buffer specifying initial data
				V( pd3dDev->CreateBuffer(&bd, &InitData, &m_pVertStream) );


				// Set initial data info
				InitData.pSysMem = m_iIndices;

				// Fill DX11 vertex buffer description
				bd.ByteWidth = sizeof(unsigned int) * m_iNrFaces * 3;
				bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
				V( pd3dDev->CreateBuffer(&bd, &InitData, &m_pIndexStream) );

			}
		}

		// 
		if(bGenQuadTree)
		{
			m_pQuadTree = new CQuadTree;
			if(m_pQuadTree!=NULL && m_pQuadTree->InitTree(m_iNrFaces))
			{
				for(int t=0; t<m_iNrFaces; t++)
					m_pQuadTree->AddTriangle(m_vVerts[m_iIndices[t*3+0]].pos, m_vVerts[m_iIndices[t*3+1]].pos, m_vVerts[m_iIndices[t*3+2]].pos);
				bool bRes = m_pQuadTree->BuildTree();
			}
		}


		if(m_vVerts!=NULL) { delete [] m_vVerts; m_vVerts=NULL; }
		if(m_iIndices!=NULL) { delete [] m_iIndices; m_iIndices=NULL; }
	}

	return bSuccess;
}

float CMeshFil::QueryTopY(const float fX, const float fZ) const
{
	float fRes = -10000000000.0f;
	if(m_pQuadTree!=NULL)
		fRes = m_pQuadTree->QueryTopY(fX, fZ);

	return fRes;
}

void CMeshFil::CleanUp()
{
	if(m_pVertStream!=NULL) SAFE_RELEASE( m_pVertStream );
	if(m_pIndexStream!=NULL) SAFE_RELEASE( m_pIndexStream );
}




CMeshFil::CMeshFil()
{
	m_iNrVerts = 0;
	m_iNrFaces = 0;
	m_vVerts = NULL;
	m_iIndices = NULL;

	m_pVertStream = NULL;
	m_pIndexStream = NULL;

	m_vMin = Vec3(0,0,0);
	m_vMax = Vec3(0,0,0);
}


CMeshFil::~CMeshFil()
{


}