#include "mesh_bvh.h"

using namespace LiteMath;

void MeshBVH::init(const cmesh4::SimpleMesh &_mesh)
{
  mesh = _mesh;

  m_device = rtcNewDevice(nullptr);
  m_scene = rtcNewScene(m_device);
  m_geometry = rtcNewGeometry(m_device, RTC_GEOMETRY_TYPE_TRIANGLE);
  float* vertices   = (float*)    rtcSetNewGeometryBuffer(m_geometry, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 3*sizeof(float),    mesh.VerticesNum());
  unsigned* indices = (unsigned*) rtcSetNewGeometryBuffer(m_geometry, RTC_BUFFER_TYPE_INDEX,  0, RTC_FORMAT_UINT3,  3*sizeof(unsigned), mesh.TrianglesNum());
  
  for (int i=0; i<mesh.VerticesNum(); i++)
    for (int j=0;j<3;j++)
      vertices[3*i + j] = mesh.vPos4f[i][j];

  memcpy(indices, mesh.indices.data(), mesh.IndicesNum()*sizeof(unsigned));

  rtcSetSceneBuildQuality(m_scene, RTC_BUILD_QUALITY_MEDIUM);
  rtcAttachGeometry(m_scene, m_geometry);
  rtcReleaseGeometry(m_geometry);
  rtcCommitGeometry(m_geometry);
  rtcCommitScene(m_scene);
}

MeshBVH::~MeshBVH()
{
  rtcReleaseScene(m_scene);
  rtcReleaseDevice(m_device);
}

static float3 closest_point_triangle(const float3 &p, const float3 &a, const float3 &b, const float3 &c)
{
  // implementation taken from Embree library
  const float3 ab = b - a;
  const float3 ac = c - a;
  const float3 ap = p - a;

  const float d1 = dot(ab, ap);
  const float d2 = dot(ac, ap);
  if (d1 <= 0.f && d2 <= 0.f)
    return a; // #1

  const float3 bp = p - b;
  const float d3 = dot(ab, bp);
  const float d4 = dot(ac, bp);
  if (d3 >= 0.f && d4 <= d3)
    return b; // #2

  const float3 cp = p - c;
  const float d5 = dot(ab, cp);
  const float d6 = dot(ac, cp);
  if (d6 >= 0.f && d5 <= d6)
    return c; // #3

  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f)
  {
    const float v = d1 / (d1 - d3);
    return a + v * ab; // #4
  }

  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f)
  {
    const float v = d2 / (d2 - d6);
    return a + v * ac; // #5
  }

  const float va = d3 * d6 - d5 * d4;
  if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f)
  {
    const float v = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return b + v * (c - b); // #6
  }

  const float denom = 1.f / (va + vb + vc);
  const float v = vb * denom;
  const float w = vc * denom;
  return a + v * ab + w * ac; // #0
}

struct SignedDistanceQueryCtx
{
  cmesh4::SimpleMesh *mesh;
  unsigned triangle_id = 0;
  float distance = 1000;
  float3 pt;
};

bool signed_distance_query_function(RTCPointQueryFunctionArguments *args)
{
  assert(args->userPtr);
  SignedDistanceQueryCtx *ctx = (SignedDistanceQueryCtx *)(args->userPtr);
  cmesh4::SimpleMesh *mesh = ctx->mesh;

  unsigned idx0 = mesh->indices[3*args->primID+0];
  unsigned idx1 = mesh->indices[3*args->primID+1];
  unsigned idx2 = mesh->indices[3*args->primID+2];

  float3 p = float3(args->query->x, args->query->y, args->query->z);
  float3 pt = closest_point_triangle(p, to_float3(mesh->vPos4f[idx0]), to_float3(mesh->vPos4f[idx1]), to_float3(mesh->vPos4f[idx2]));
  float d = length(p - pt);
  
  //if (args->primID < 1000)
  //printf("distance %f to %u\n",d, args->primID);

  if (d < ctx->distance)
  {
    ctx->distance = d;
    ctx->triangle_id = args->primID;
    ctx->pt = pt;
    args->query->radius = d;
    return true;
  }

  return false;
}

float MeshBVH::get_signed_distance(LiteMath::float3 p)
{
  RTCPointQueryContext ctx;
  RTCPointQuery q;
  SignedDistanceQueryCtx sdq_ctx;

  rtcInitPointQueryContext(&ctx);

  q.x = p.x;
  q.y = p.y;
  q.z = p.z;
  q.radius = 1e10f;
  q.time = 0.0f;

  sdq_ctx.mesh = &mesh;

  rtcPointQuery(m_scene, &q, &ctx, signed_distance_query_function, &sdq_ctx);

  unsigned idx0 = mesh.indices[3*sdq_ctx.triangle_id+0];
  unsigned idx1 = mesh.indices[3*sdq_ctx.triangle_id+1];
  unsigned idx2 = mesh.indices[3*sdq_ctx.triangle_id+2];

  float3 p0 = to_float3(mesh.vPos4f[idx0]);
  float3 p1 = to_float3(mesh.vPos4f[idx1]);
  float3 p2 = to_float3(mesh.vPos4f[idx2]);

  float3 n = normalize(cross(p1-p0, p2-p0));
  float3 dir = p-sdq_ctx.pt;

  if (dot(dir, n) < 0)
    sdq_ctx.distance = -sdq_ctx.distance;

  //printf("closest triangle %u with d=%f\n", sdq_ctx.triangle_id, sdq_ctx.distance);

  return sdq_ctx.distance;
}