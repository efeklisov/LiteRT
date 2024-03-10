#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cfloat>
#include <memory>

#include "BVH2Common.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

constexpr size_t reserveSize = 1000;

void BVHRT::ClearGeom()
{
  m_vertPos.reserve(std::max<size_t>(100000, m_vertPos.capacity()));
  m_indices.reserve(std::max<size_t>(100000 * 3, m_indices.capacity()));
  m_primIndices.reserve(std::max<size_t>(100000, m_primIndices.capacity()));

  m_vertPos.resize(0);
  m_indices.resize(0);
  m_primIndices.resize(0);

  m_allNodePairs.reserve(std::max<size_t>(100000, m_allNodePairs.capacity()));
  m_allNodePairs.resize(0);

  m_geomOffsets.reserve(std::max(reserveSize, m_geomOffsets.capacity()));
  m_geomOffsets.resize(0);

  m_geomBoxes.reserve(std::max<size_t>(reserveSize, m_geomBoxes.capacity()));
  m_geomBoxes.resize(0);

  m_bvhOffsets.reserve(std::max<size_t>(reserveSize, m_bvhOffsets.capacity()));
  m_bvhOffsets.resize(0);

  m_SdfScene = SdfScene();

  ClearScene();
}

void BVHRT::AppendTreeData(const std::vector<BVHNodePair>& a_nodes, const std::vector<uint32_t>& a_indices, const uint32_t *a_triIndices, size_t a_indNumber)
{
  m_allNodePairs.insert(m_allNodePairs.end(), a_nodes.begin(), a_nodes.end());
  m_primIndices.insert(m_primIndices.end(), a_indices.begin(), a_indices.end());
  
  const size_t oldIndexSize  = m_indices.size();
  m_indices.resize(oldIndexSize + a_indices.size()*3);
  for(size_t i=0;i<a_indices.size();i++)
  {
    const uint32_t triId = a_indices[i];
    m_indices[oldIndexSize + 3*i+0] = a_triIndices[triId*3+0];
    m_indices[oldIndexSize + 3*i+1] = a_triIndices[triId*3+1];
    m_indices[oldIndexSize + 3*i+2] = a_triIndices[triId*3+2];
  }
}

uint32_t BVHRT::AddGeom_Triangles3f(const float *a_vpos3f, size_t a_vertNumber, const uint32_t *a_triIndices, size_t a_indNumber, BuildQuality a_qualityLevel, size_t vByteStride)
{
  const size_t vStride = vByteStride / 4;
  assert(vByteStride % 4 == 0);

  const uint32_t currGeomId = uint32_t(m_geomOffsets.size());
  const size_t oldSizeVert  = m_vertPos.size();
  const size_t oldSizeInd   = m_indices.size();

  m_geomOffsets.push_back(uint2(oldSizeInd, oldSizeVert));

  m_vertPos.resize(oldSizeVert + a_vertNumber);

  Box4f bbox;
  for (size_t i = 0; i < a_vertNumber; i++)
  {
    const float4 v = float4(a_vpos3f[i * vStride + 0], a_vpos3f[i * vStride + 1], a_vpos3f[i * vStride + 2], 1.0f);
    m_vertPos[oldSizeVert + i] = v;
    bbox.include(v);
  }

  m_geomBoxes.push_back(bbox);
  m_geomTypeByGeomId.push_back(GeometryType::MESH_TRIANGLE);

  // Build BVH for each geom and append it to big buffer;
  // append data to global arrays and fix offsets
  //
  const size_t oldBvhSize = m_allNodePairs.size();
  m_bvhOffsets.push_back(oldBvhSize);
  
  auto presets = bvh::BuilderPresetsFromString(m_buildName.c_str());
  auto layout  = bvh::LayoutPresetsFromString(m_layoutName.c_str());
  auto bvhData = bvh::BuildBVHFat((const float*)(m_vertPos.data() + oldSizeVert), a_vertNumber, 16, a_triIndices, a_indNumber, presets, layout);

  AppendTreeData(bvhData.nodes, bvhData.indices, a_triIndices, a_indNumber);

  return currGeomId;
}

void BVHRT::UpdateGeom_Triangles3f(uint32_t a_geomId, const float *a_vpos3f, size_t a_vertNumber, const uint32_t *a_triIndices, size_t a_indNumber, BuildQuality a_qualityLevel, size_t vByteStride)
{
  std::cout << "[BVHRT::UpdateGeom_Triangles3f]: " << "not implemeted!" << std::endl; // not planned for this implementation (possible in general)
}

uint32_t BVHRT::AddGeom_Sdf(const SdfScene &scene, BuildQuality a_qualityLevel)
{
  assert(scene.conjunctions.size() > 0);
  assert(scene.objects.size() > 0);
  assert(scene.parameters.size() > 0);
  float3 mn = scene.conjunctions[0].bbox.min_pos;
  float3 mx = scene.conjunctions[0].bbox.max_pos;
  for (auto &c : scene.conjunctions) 
  {
    mn = min(mn, c.bbox.min_pos);
    mx = max(mx, c.bbox.max_pos);
  }
  LiteMath::AABB aabb(mn, mx);
  m_geomOffsets.push_back(uint2(m_ConjIndices.size(), 0));
  m_geomBoxes.push_back(Box4f(LiteMath::to_float4(aabb.min_pos, 1), LiteMath::to_float4(aabb.max_pos, 1)));
  m_geomTypeByGeomId.push_back(GeometryType::SDF_PRIMITIVE);
  m_bvhOffsets.push_back(m_allNodePairs.size());

  unsigned p_offset = m_SdfScene.parameters.size();
  unsigned o_offset = m_SdfScene.objects.size();
  unsigned c_offset = m_SdfScene.conjunctions.size();

  m_SdfScene.parameters.insert(m_SdfScene.parameters.end(), scene.parameters.begin(), scene.parameters.end());
  m_SdfScene.objects.insert(m_SdfScene.objects.end(), scene.objects.begin(), scene.objects.end());
  m_SdfScene.conjunctions.insert(m_SdfScene.conjunctions.end(), scene.conjunctions.begin(), scene.conjunctions.end());

  for (int i=o_offset;i<m_SdfScene.objects.size();i++)
    m_SdfScene.objects[i].params_offset += p_offset;
  
  for (int i=c_offset;i<m_SdfScene.conjunctions.size();i++)
    m_SdfScene.conjunctions[i].offset += o_offset;

  std::vector<unsigned> conj_indices;
  std::vector<bvh::BVHNode> orig_nodes;
  for (int i=0;i<scene.conjunctions.size();i++)
  {
    auto &c = scene.conjunctions[i];
    conj_indices.push_back(c_offset + i);
    orig_nodes.emplace_back();
    orig_nodes.back().boxMin = c.bbox.min_pos;
    orig_nodes.back().boxMax = c.bbox.max_pos;
  }
  m_ConjIndices.insert(m_ConjIndices.end(), conj_indices.begin(), conj_indices.end());
  //orig_nodes.resize(1);
  //orig_nodes[0].boxMin = aabb.min_pos;
  //orig_nodes[0].boxMax = aabb.max_pos;

  // Build BVH for each geom and append it to big buffer;
  // append data to global arrays and fix offsets
  auto presets = bvh::BuilderPresetsFromString(m_buildName.c_str());
  auto layout  = bvh::LayoutPresetsFromString(m_layoutName.c_str());
  auto bvhData = bvh::BuildBVHFatCustom(orig_nodes.data(), orig_nodes.size(), presets, layout);

  for (auto &i : bvhData.indices)
    printf("ind %d\n",(int)i);

  m_allNodePairs.insert(m_allNodePairs.end(), bvhData.nodes.begin(), bvhData.nodes.end());

  return m_geomTypeByGeomId.size()-1;
}

void BVHRT::ClearScene()
{
  m_instBoxes.reserve(std::max(reserveSize, m_instBoxes.capacity()));
  m_instMatricesInv.reserve(std::max(reserveSize, m_instMatricesInv.capacity()));
  m_instMatricesFwd.reserve(std::max(reserveSize, m_instMatricesFwd.capacity()));

  m_geomIdByInstId.reserve(std::max(reserveSize, m_geomIdByInstId.capacity()));

  m_instBoxes.resize(0);
  m_instMatricesInv.resize(0);
  m_instMatricesFwd.resize(0);
  m_geomIdByInstId.resize(0);

  m_firstSceneCommit = true;
}

void DebugPrintNodes(const std::vector<BVHNode>& nodes, const std::string& a_fileName)
{
  std::ofstream fout(a_fileName.c_str());

  for(size_t i=0;i<nodes.size();i++)
  {
    const auto& currBox = nodes[i];
    fout << "node[" << i << "]:" << std::endl;
    fout << "  bmin = { " << currBox.boxMin[0] << " " << currBox.boxMin[1] << " " << currBox.boxMin[2] << " } | " << currBox.leftOffset  << std::endl;
    fout << "  bmax = { " << currBox.boxMax[0] << " " << currBox.boxMax[1] << " " << currBox.boxMax[2] << " } | " << currBox.escapeIndex << std::endl;
  } 
}

void DebugPrintBoxes(const std::vector<Box4f>& nodes, const std::string& a_fileName)
{
  std::ofstream fout(a_fileName.c_str());

  for(size_t i=0;i<nodes.size();i++)
  {
    const auto& currBox = nodes[i];
    fout << "node[" << i << "]:" << std::endl;
    fout << "  bmin = { " << currBox.boxMin[0] << " " << currBox.boxMin[1] << " " << currBox.boxMin[2] << " " << currBox.boxMin[3]  << std::endl;
    fout << "  bmax = { " << currBox.boxMax[0] << " " << currBox.boxMax[1] << " " << currBox.boxMax[2] << " " << currBox.boxMax[3] << std::endl;
  } 
}

void BVHRT::CommitScene(BuildQuality a_qualityLevel)
{
  bvh::BuilderPresets presets = {bvh::BVH2_LEFT_OFFSET, bvh::BVHQuality::HIGH, 1};
  m_nodesTLAS = bvh::BuildBVH((const bvh::BVHNode *)m_instBoxes.data(), m_instBoxes.size(), presets).nodes;

  // DebugPrintNodes(m_nodesTLAS, "z01_tlas.txt");
  // DebugPrintBoxes(m_instBoxes, "y01_boxes.txt");

  m_firstSceneCommit = false;
}

uint32_t BVHRT::AddInstance(uint32_t a_geomId, const float4x4 &a_matrix)
{
  const auto &box = m_geomBoxes[a_geomId];

  // (1) mult mesh bounding box vertices with matrix to form new bouding box for instance
  float4 boxVertices[8]{
      a_matrix * float4{box.boxMin.x, box.boxMin.y, box.boxMin.z, 1.0f},
      a_matrix * float4{box.boxMax.x, box.boxMin.y, box.boxMin.z, 1.0f},
      a_matrix * float4{box.boxMin.x, box.boxMax.y, box.boxMin.z, 1.0f},
      a_matrix * float4{box.boxMin.x, box.boxMin.y, box.boxMax.z, 1.0f},
      a_matrix * float4{box.boxMax.x, box.boxMax.y, box.boxMin.z, 1.0f},
      a_matrix * float4{box.boxMax.x, box.boxMin.y, box.boxMax.z, 1.0f},
      a_matrix * float4{box.boxMin.x, box.boxMax.y, box.boxMax.z, 1.0f},
      a_matrix * float4{box.boxMax.x, box.boxMax.y, box.boxMax.z, 1.0f},
  };

  Box4f newBox;
  for (size_t i = 0; i < 8; i++)
    newBox.include(boxVertices[i]);

  // (2) append bounding box and matrices
  //
  const uint32_t oldSize = uint32_t(m_instBoxes.size());

  m_instBoxes.push_back(newBox);
  m_instMatricesFwd.push_back(a_matrix);
  m_instMatricesInv.push_back(inverse4x4(a_matrix));
  m_geomIdByInstId.push_back(a_geomId);

  return oldSize;
}

void BVHRT::UpdateInstance(uint32_t a_instanceId, const float4x4 &a_matrix)
{
  if(a_instanceId > m_geomIdByInstId.size())
  {
    std::cout << "[BVHRT::UpdateInstance]: " << "bad instance id == " << a_instanceId << "; size == " << m_geomIdByInstId.size() << std::endl;
    return;
  }

  const uint32_t geomId = m_geomIdByInstId[a_instanceId];
  const float4 boxMin   = m_geomBoxes[geomId].boxMin;
  const float4 boxMax   = m_geomBoxes[geomId].boxMax;

  // (1) mult mesh bounding box vertices with matrix to form new bouding box for instance
  float4 boxVertices[8]{
      a_matrix * float4{boxMin.x, boxMin.y, boxMin.z, 1.0f},
      a_matrix * float4{boxMax.x, boxMin.y, boxMin.z, 1.0f},
      a_matrix * float4{boxMin.x, boxMax.y, boxMin.z, 1.0f},
      a_matrix * float4{boxMin.x, boxMin.y, boxMax.z, 1.0f},
      a_matrix * float4{boxMax.x, boxMax.y, boxMin.z, 1.0f},
      a_matrix * float4{boxMax.x, boxMin.y, boxMax.z, 1.0f},
      a_matrix * float4{boxMin.x, boxMax.y, boxMax.z, 1.0f},
      a_matrix * float4{boxMax.x, boxMax.y, boxMax.z, 1.0f},
  };

  Box4f newBox;
  for (size_t i = 0; i < 8; i++)
    newBox.include(boxVertices[i]);

  m_instBoxes      [a_instanceId] = newBox;
  m_instMatricesFwd[a_instanceId] = a_matrix;
  m_instMatricesInv[a_instanceId] = inverse4x4(a_matrix);
}

ISceneObject* MakeBVH2CommonRT(const char* a_implName, const char* a_buildName, const char* a_layoutName) 
{
  return new BVHRT(a_buildName, a_layoutName); 
}