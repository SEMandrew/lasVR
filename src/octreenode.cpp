#include "global.h"
#include "staticfunctions.h"
#include "octreenode.h"

#include <QMessageBox>
#include <QtMath>
#include <QFile>
#include <QFileInfo>

#include "laszip_dll.h"


OctreeNode::OctreeNode()
{
  m_parent = 0;
  m_removalFlag = false;
  m_priority = 0;
  m_time = -1;
  m_id = -1;
  m_uid = -1;
  m_active = false;
  m_fileName.clear();
  m_bmin = m_bmax = Vec(0,0,0);
  m_bminO = m_bmaxO = Vec(0,0,0);
  m_offset = Vec(0,0,0);
  m_numpoints = 0;
  m_coord = 0;
  for(int i=0; i<8; i++)
    m_child[i] = 0;
  m_levelsBelow = -1;
  m_level = -1;
  m_maxVisLevel = 0;
  m_dataLoaded = false;
  m_levelString.clear();
  m_dpv = 3;

  m_rotation = Quaternion();
  m_scale = 1.0;
  m_scaleCloudJs = 1.0;
  m_shift = Vec(0,0,0);
  m_xformCen = Vec(0,0,0);

  m_pointSize = 1.0;
  m_spacing = 1.0;

  m_globalMin = Vec(0,0,0);
  m_globalMax = Vec(0,0,0);

  m_pointAttrib.clear();
  m_attribBytes = 0;

  m_editMode = false;

  m_bminZ = 1;
  m_bmaxZ = 0;
}

OctreeNode::~OctreeNode()
{
  m_parent = 0;
  m_removalFlag = false;
  m_priority = 0;
  m_time = -1;
  m_id = -1;
  m_active = false;
  m_fileName.clear();
  m_bmin = m_bmax = Vec(0,0,0);
  m_offset = Vec(0,0,0);
  m_numpoints = 0;

  if (m_coord)
    delete [] m_coord;
  m_coord = 0;

  for(int i=0; i<8; i++)
    m_child[i] = 0;

  m_levelsBelow = -1;
  m_level = -1;
  m_maxVisLevel = 0;
  m_dataLoaded = false;
  m_levelString.clear();

  m_rotation = Quaternion();
  m_scale = 1.0;
  m_scaleCloudJs = 1.0;
  m_shift = Vec(0,0,0);
  m_xformCen = Vec(0,0,0);

  m_pointSize = 1.0;
  m_spacing = 1.0;

  m_pointAttrib.clear();
  m_attribBytes = 0;

  m_editMode = false;

  m_bminZ = 1;
  m_bmaxZ = 0;
}

void
OctreeNode::setGlobalMinMax(Vec gmin, Vec gmax)
{
  m_globalMin = gmin;
  m_globalMax = gmax;    

  m_bmin = xformPoint(m_bminO);
  m_bmax = xformPoint(m_bmaxO);

  m_tightMin = xformPoint(m_tightMinO);
  m_tightMax = xformPoint(m_tightMaxO);
}

Vec
OctreeNode::xformPoint(Vec v)
{
  Vec ov = v-m_xformCen;

  ov = m_rotation.rotate(ov);

  ov *= m_scale;

  ov += m_xformCen;
  
  ov += m_shift;

  ov -= m_globalMin;

  return ov;
}

void
OctreeNode::setScale(float scl, float sclCloudJs)
{
  m_scale = scl;
  m_scaleCloudJs = sclCloudJs;

  m_bmin = xformPoint(m_bminO);
  m_bmax = xformPoint(m_bmaxO);
  m_tightMin = xformPoint(m_tightMinO);
  m_tightMax = xformPoint(m_tightMaxO);
}

void
OctreeNode::setShift(Vec s)
{
  m_shift = s;

  setScale(m_scale, m_scaleCloudJs);
}

void
OctreeNode::setRotation(Quaternion q)
{
  m_rotation = q;
  
  setScale(m_scale, m_scaleCloudJs);
}

void
OctreeNode::setXform(float scale, Vec shift, Quaternion rotate, Vec xformCen)
{
  m_scale = scale;
  m_shift = shift;
  m_rotation = rotate;
  m_xformCen = xformCen;

  m_bmin = xformPoint(m_bminO);
  m_bmax = xformPoint(m_bmaxO);
  m_tightMin = xformPoint(m_tightMinO);
  m_tightMax = xformPoint(m_tightMaxO);
}

bool
OctreeNode::inBox(Vec pt)
{
  if (pt.x < m_bmin.x || pt.y < m_bmin.y || pt.z < m_bmin.z ||
      pt.x > m_bmax.x || pt.y > m_bmax.y || pt.z > m_bmax.z)
    return false;

  return true;
}

bool
OctreeNode::inBoxXY(Vec pt)
{
  if (pt.x < m_bmin.x || pt.y < m_bmin.y ||
      pt.x > m_bmax.x || pt.y > m_bmax.y)
    return false;

  return true;
}

void
OctreeNode::markForDeletion()
{
  for (int k=0; k<8; k++)
    {
      if (m_child[k] != 0)
	{
	  m_child[k]->markForDeletion();
	  m_child[k] = 0;
	}
    }
  m_removalFlag = true;
}

bool
OctreeNode::isLeaf()
{
  for(int i=0; i<8; i++)
    if (m_child[i] != 0)
      return false;

  return true;
}

OctreeNode*
OctreeNode::childAt(int i)
{
  if (i<0 || i>7)
    {
      QMessageBox::information(0, "", QString("Octree Index %1 out of range").arg(i));
      return NULL;
    }

  OctreeNode *node = m_child[i];

  if (!node)
    {
      node = new OctreeNode();
      addChild(i, node);

      node->setParent(this);
    }

  return node;
}

void
OctreeNode::loadData()
{
  if (markedForDeletion())
    {
      m_numpoints = 0;
      return;
    }

//  if (m_bmin.x >= m_bmax.x ||
//      m_bmin.y >= m_bmax.y ||
//      m_bmin.z >= m_bmax.z)
//    {
//      m_numpoints = 0;
//      return;      
//    }

  if (m_dataLoaded)
    return;

  if (m_attribBytes == 0)
    loadDataFromLASFile();
  else
    loadDataFromBINFile();

  m_dataLoaded = true;
}

void
OctreeNode::reloadData()
{
  if (m_dataLoaded)
    {
      unloadData();
      loadData();
    }
}

void
OctreeNode::unloadData()
{
  m_dataLoaded = false;

  if (m_coord)
    delete [] m_coord;
  m_coord = 0;
}

void
OctreeNode::loadDataFromBINFile()
{
  if (markedForDeletion())
    {
      m_numpoints = 0;
      return;
    }

  QFileInfo finfo(m_fileName);
  qint64 fsz = finfo.size();
  m_numpoints = fsz/m_attribBytes;
  

  if (m_dpv == 3)
    {
      if (!m_coord)
	m_coord = new uchar[m_numpoints*m_dpv*sizeof(float)];
      memset(m_coord, m_numpoints*m_dpv*sizeof(float), 0);
    }
  else
    {
      // vertex as float and color stored as uchar
      // treating color as 4 unsigned shorts
      if (!m_coord)
	m_coord = new uchar[20*m_numpoints];
      memset(m_coord, 20*m_numpoints, 0);
    }

  QList<Vec> colorMap = Global::getColorMap();
  int clim = colorMap.count()-1;

  uchar *data = new uchar[fsz];
  
  QFile binfl(m_fileName);
  binfl.open(QFile::ReadOnly);
  binfl.read((char*)data, fsz);
  binfl.close();


  float gminZ,gmaxZ;
  if (!m_editMode)
    {
      //gminZ = 0;
      //gmaxZ = m_globalMax.z - m_globalMin.z;
      gminZ = 0;
      gmaxZ = m_bmaxZ - m_bminZ;
    }
  else
    {
      //gminZ = m_globalMin.z;
      //gmaxZ = m_globalMax.z;
      gminZ = m_bminZ;
      gmaxZ = m_bmaxZ;
    }

  for(qint64 np = 0; np < m_numpoints; np++)
    {
      int *crd = (int*)(data + m_attribBytes*np);
      uchar *rgb = (uchar*)(data + m_attribBytes*np + 12);

      double x, y, z;
      x = ((double)crd[0] * m_scaleCloudJs) + m_offset.x;
      y = ((double)crd[1] * m_scaleCloudJs) + m_offset.y;
      z = ((double)crd[2] * m_scaleCloudJs) + m_offset.z;

      if (!m_editMode)
	{
	  Vec ve = Vec(x,y,z);
	  ve = xformPoint(ve);
	  x = ve.x;
	  y = ve.y;
	  z = ve.z;
	}

      if (m_dpv == 3)
	{
	  float *vertexPtr = (float*)(m_coord + 12*np);
	  vertexPtr[0] = x;
	  vertexPtr[1] = y;
	  vertexPtr[2] = z;
	}
      
      if (m_dpv > 3)
	{
	  float *vertexPtr = (float*)(m_coord + 20*np);
	  
	  //-------------------------------------------
	  // shift data
	  vertexPtr[0] = x;
	  vertexPtr[1] = y;
	  vertexPtr[2] = z;
	  //-------------------------------------------

	  Vec col = Vec(255,255,255);
	  if (m_attribBytes > 15)
	    col = Vec(rgb[0],rgb[1],rgb[2]);

	  if (!m_colorPresent)
	    {
	      //z = (z-m_globalMin.z)/(m_globalMax.z-m_globalMin.z);
	      z = (z-gminZ)/(gmaxZ-gminZ);

	      z = qBound(0.0, z, 1.0);
	      z*=clim;
	      int zi = z;
	      float zf = z - zi;
	      if (zi >= clim) { zi = clim-1; zf = 1.0; }
	      col = colorMap[zi]*(1.0-zf) + zf*colorMap[zi+1];
	      col *= 255;
	    }
	  
	  // color stored as ushort
	  ushort *colorPtr = (ushort*)(m_coord + 20*np + 12);
	  colorPtr[0] = col.x;
	  colorPtr[1] = col.y;
	  colorPtr[2] = col.z;
	  colorPtr[3] = m_id; // assuming id values are less than 65536
	}
    }

  delete [] data;
}



void
OctreeNode::loadDataFromLASFile()
{
  if (markedForDeletion())
    {
      m_numpoints = 0;
      return;
    }

  laszip_POINTER laszip_reader;
  
  laszip_create(&laszip_reader);

  laszip_BOOL is_compressed = m_fileName.endsWith(".laz");
  if (laszip_open_reader(laszip_reader, m_fileName.toLatin1().data(), &is_compressed))
    {
      QMessageBox::information(0, m_fileName, "Error opening file "+m_fileName);
    }
  
  laszip_header* header;
  laszip_get_header_pointer(laszip_reader, &header);

  laszip_I64 npts = (header->number_of_point_records ? header->number_of_point_records : header->extended_number_of_point_records);
  
  // get a pointer to the points that will be read  
  laszip_point* point;
  laszip_get_point_pointer(laszip_reader, &point);


  if (m_dpv == 3)
    {
      if (!m_coord)
	m_coord = new uchar[npts*m_dpv*sizeof(float)];
      memset(m_coord, npts*m_dpv*sizeof(float), 0);
    }
  else
    {
      // vertex as float and color stored as uchar
      // treating color as 4-bytes for easy alignment
//      if (!m_coord)
//	m_coord = new uchar[16*npts];
//      memset(m_coord, 16*npts, 0);

      // vertex as float and color stored as uchar
      // treating color as 4 unsigned shorts
      if (!m_coord)
	m_coord = new uchar[20*npts];
      memset(m_coord, 20*npts, 0);
    }


  QList<Vec> colorMap = Global::getColorMap();
  int clim = colorMap.count()-1;

  float gminZ,gmaxZ;
  if (!m_editMode)
    {
      //gminZ = 0;
      //gmaxZ = m_globalMax.z - m_globalMin.z;
      gminZ = 0;
      gmaxZ = m_bmaxZ - m_bminZ;
    }
  else
    {
      //gminZ = m_globalMin.z;
      //gmaxZ = m_globalMax.z;
      gminZ = m_bminZ;
      gmaxZ = m_bmaxZ;
    }

  qint64 np = 0;
  for(qint64 i = 0; i < npts; i++)
    {
      // read a point
      laszip_read_point(laszip_reader);
      
//      if (m_dpv != 4 ||
//	  (point->classification != 7 && // condition specifically for ACT data 
//	   point->classification != 18) )
//      if (point->classification != 7 && // condition specifically for ACT data 
//	  point->classification != 18)
	{
	  double x, y, z;
	  x = ((double)point->X * m_scaleCloudJs) + m_offset.x;
	  y = ((double)point->Y * m_scaleCloudJs) + m_offset.y;
	  z = ((double)point->Z * m_scaleCloudJs) + m_offset.z;
	  
	  if (!m_editMode)
	    {
	      Vec ve = Vec(x,y,z);
	      ve = xformPoint(ve);
	      x = ve.x;
	      y = ve.y;
	      z = ve.z;
	    }	  

	  if (m_dpv == 3)
	    {
	      float *vertexPtr = (float*)(m_coord + 12*np);
	      vertexPtr[0] = x;
	      vertexPtr[1] = y;
	      vertexPtr[2] = z;
	    }

	  if (m_dpv > 3)
	    {
	      //float *vertexPtr = (float*)(m_coord + 16*np);
	      float *vertexPtr = (float*)(m_coord + 20*np);
	      
	      //-------------------------------------------
	      // shift data
	      vertexPtr[0] = x;
	      vertexPtr[1] = y;
	      vertexPtr[2] = z;
	      //-------------------------------------------

	      Vec col = Vec(1,1,1);

	      if (!m_classPresent &&
		  !m_colorPresent)
		{
		  //z = (z-m_globalMin.z)/(m_globalMax.z-m_globalMin.z);
		  z = (z-gminZ)/(gmaxZ-gminZ);
		  
		  z = qBound(0.0, z, 1.0);

//		  float zsign = (z < 0.5 ? -1 : 1);
//		  z = qAbs(z-0.5)*2;
//		  z = StaticFunctions::easeOut(StaticFunctions::easeOut(z));
		  z*=clim;
		  int zi = z;
		  float zf = z - zi;
		  if (zi >= clim) { zi = clim-1; zf = 1.0; }

		  col = colorMap[zi]*(1.0-zf) + zf*colorMap[zi+1];

		  col *= 255;
		}
	      else if (m_classPresent)
		{
		  int idx = qBound(0, (int)(point->classification), clim);
		  Vec col0 = colorMap[idx];
		  
		  //z = (z-m_globalMin.z)/(m_globalMax.z-m_globalMin.z);
		  z = (z-gminZ)/(gmaxZ-gminZ);

		  z = 0.5+(z-0.5 < 0 ? -1 : z-0.5 > 0 ? 1 : 0)*qPow(qAbs(z-0.5f), 0.4f);
		  z = qBound(0.0, z, 1.0);
		  z*=clim;
		  int zi = z;
		  float zf = z - zi;
		  if (zi >= clim) { zi = clim-1; zf = 1.0; }
		  Vec col1 = colorMap[zi]*(1.0-zf) + zf*colorMap[zi+1];
		  
		  col = col0*0.5 + col1*0.5;
		  col *= 255;
		}
	      else if (m_colorPresent)
		{
		  ushort r,g,b;
		  r = point->rgb[0];
		  g = point->rgb[1];
		  b = point->rgb[2];
		  if(r > 255 || g > 255 || b > 255)
		    {
		      r /= 256;
		      g /= 256;
		      b /= 256;
		    }
		  col = Vec(r,g,b);
		}
	      
	      // color stored as uchar
	      //m_coord[16*np+12] = col.x;
	      //m_coord[16*np+13] = col.y;
	      //m_coord[16*np+14] = col.z;
	      //m_coord[16*np+15] = m_id; // assuming id values are less than 256

	      // color stored as ushort
	      ushort *colorPtr = (ushort*)(m_coord + 20*np + 12);
	      colorPtr[0] = col.x;
	      colorPtr[1] = col.y;
	      colorPtr[2] = col.z;
	      colorPtr[3] = m_id; // assuming id values are less than 65536
	    }

	  np++;
	}
    }
  //-------------
  // update numpts based on valid points read
  m_numpoints = np;
  //-------------

  laszip_close_reader(laszip_reader);
}

void
OctreeNode::setId(int id)
{
  // set same id for all nodes in the same tree
  m_id = id;

  if (!isLeaf())
    {
      for(int k=0; k<8; k++)
	{
	  OctreeNode *cnode = getChild(k);
	  if (cnode)
	    cnode->setId(id);
	}
    }
}

QList<OctreeNode*>
OctreeNode::allActiveNodes()
{
  QList<OctreeNode*> activeNodes;

  if (!isActive())
    return activeNodes;
  
  activeNodes << this;

  int an = 0;

  bool done = false;
  while (!done)
    {
      OctreeNode *node = activeNodes[an];
      an++;

      if (!node->isLeaf())
	{
	  for(int k=0; k<8; k++)
	    {
	      OctreeNode *cnode = node->getChild(k);
	      if (cnode != 0 &&
		  cnode->isActive())
		activeNodes << cnode;
	    }
	}

      if (an >= activeNodes.count())
	done = true;
    }

  return activeNodes;
}

int
OctreeNode::setPointSizeForActiveNodes(float ps)
{
  if (!isActive())
    return 0;
  
  QList<OctreeNode*> activeNodes;
  activeNodes << this;

  int deepestVisibleLevel = 0;

  int an = 0;
  bool done = false;
  while (!done)
    {
      OctreeNode *node = activeNodes[an];
      an++;

      deepestVisibleLevel = qMax(deepestVisibleLevel, node->level());

      if (!node->isLeaf())
	{
	  for(int k=0; k<8; k++)
	    {
	      OctreeNode *cnode = node->getChild(k);
	      if (cnode != 0 &&
		  cnode->isActive())
		activeNodes << cnode;
	    }
	}

      if (an >= activeNodes.count())
	done = true;
    }

  for(int i=0; i<activeNodes.count(); i++)
    //activeNodes[i]->setPointSize((deepestVisibleLevel+1.0)/ps);
    //activeNodes[i]->setPointSize(1.0/(deepestVisibleLevel+1.0));
    activeNodes[i]->setPointSize(deepestVisibleLevel);	
			 
  return (deepestVisibleLevel+1);
}

uchar
OctreeNode::setMaxVisibleLevel()
{
  if (isActive())
    m_maxVisLevel = m_level;
  else
    {
      m_maxVisLevel = 0;
      return m_maxVisLevel;
    }
  
  if (!isLeaf())
    {
      for(int k=0; k<8; k++)
	{
	  OctreeNode *cnode = getChild(k);
	  if (cnode != 0 &&
	      cnode->isActive())
	    m_maxVisLevel = qMax(m_maxVisLevel, cnode->setMaxVisibleLevel());
	}
    }

  return m_maxVisLevel;
}
