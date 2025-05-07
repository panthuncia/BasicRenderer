//============================================================================
// sphere_screen_extents
//============================================================================
// Calculates the exact screen extents xyzw=[left, bottom, right, top] in
// normalized screen coordinates [-1, 1] for a sphere in view space. For
// performance, the projection matrix (v2p) is assumed to be setup so that
// z.w=1 and w.w=0. The sphere is also assumed to be completely in front
// of the camera.
// This is an optimized implementation of paper "2D Polyhedral Bounds of a
// Clipped Perspective-Projected 3D Sphere": http://jcgt.org/published/0002/02/05/paper.pdf
float4 sphere_screen_extents(const float3 boundingSpherePos, float radius, const float4x4 mWorldToClip)
{
  // calculate horizontal extents
    //assert(v2p_.z.w == 1 && v2p_.w.w == 0);
    float4 res;
    float rad2 = radius * radius, d = boundingSpherePos.z * radius;
    float hv = sqrt(boundingSpherePos.x * boundingSpherePos.x + boundingSpherePos.z * boundingSpherePos.z - rad2);
    float ha = boundingSpherePos.x * hv, hb = boundingSpherePos.x * radius, hc = boundingSpherePos.z * hv;
    res.x = (ha - d) * mWorldToClip[0].x / (hc + hb); // left
    res.z = (ha + d) * mWorldToClip[0].x / (hc - hb); // right

  // calculate vertical extents
    float vv = sqrt(boundingSpherePos.y * boundingSpherePos.y + boundingSpherePos.z * boundingSpherePos.z - rad2);
    float va = boundingSpherePos.y * vv, vb = boundingSpherePos.y * radius, vc = boundingSpherePos.z * vv;
    res.y = (va - d) * mWorldToClip[1].y / (vc + vb); // bottom
    res.w = (va + d) * mWorldToClip[1].y / (vc - vb); // top
    return res;
}
//----------------------------------------------------------------------------