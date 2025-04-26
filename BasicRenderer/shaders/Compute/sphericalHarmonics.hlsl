//–– 0) Bindings ––
// t0: your environment cubemap (TextureCube<float4>)
// s0: sampler (SamplerState)
// u0: RWStructuredBuffer<float3>  ?  length = 9 entries
TextureCube<float4> g_envMap : register(t0);
SamplerState g_sampler : register(s0);
RWStructuredBuffer<float3> g_SH : register(u0);

// b0: root-constants: [ faceSize,    // resolution of one cubemap face
//                       weight ]      // = 4? / (faceSize*faceSize*6)
cbuffer Constants : register(b0) {
	uint faceSize;
	float weight;
}

//–– 1) Thread-group size: 16×16 threads per face, dispatch Z=6 for the 6 faces
[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
	uint x = DTid.x;
	uint y = DTid.y;
	uint face = DTid.z; // 0?5

    // out-of-bounds guard
	if (x >= faceSize || y >= faceSize)
		return;

    // 2) Compute a unit-direction from (face, u, v)
	float u = (x + 0.5f) / faceSize * 2.0f - 1.0f;
	float v = (y + 0.5f) / faceSize * 2.0f - 1.0f;
	float3 dir;
	switch (face) {
		case 0:
			dir = float3(+1, -v, -u);
			break; // +X
		case 1:
			dir = float3(-1, -v, +u);
			break; // –X
		case 2:
			dir = float3(+u, +1, +v);
			break; // +Y
		case 3:
			dir = float3(+u, -1, -v);
			break; // –Y
		case 4:
			dir = float3(+u, -v, +1);
			break; // +Z
		default:
			dir = float3(-u, -v, -1); // –Z
	}
	dir = normalize(dir);

    // 3) Sample the cubemap
	float3 L = g_envMap.SampleLevel(g_sampler, dir, 0).rgb;

    //–– 4) Evaluate the 9 real SH basis up to ?=2 (Table 20 normals)
    //    constants taken from ?((2?+1)/(4?)·(??|m|)!/(?+|m|)!)
	const float c0 = 0.28209479; // Y??
	const float c1 = 0.48860251; // Y?,±1
	const float c2 = 1.09254843; // Y?,±2
	const float c3 = 0.31539157; // Y?,0
	const float c4 = 0.54627422; // Y?,±2 (m=2 vs m=?2 share c2/c4)

	float sh[9];
	sh[0] = c0;
	sh[1] = c1 * dir.y; // Y?,?1
	sh[2] = c1 * dir.z; // Y?, 0
	sh[3] = c1 * dir.x; // Y?, 1

	sh[4] = c2 * dir.x * dir.y; // Y?,?2
	sh[5] = c2 * dir.y * dir.z; // Y?,?1
	sh[6] = c3 * (3 * dir.z * dir.z - 1); // Y?, 0
	sh[7] = c2 * dir.z * dir.x; // Y?, 1
	sh[8] = c4 * (dir.x * dir.x - dir.y * dir.y); // Y?, 2

    //–– 5) Accumulate into the global 9-entry buffer via atomic adds
    //    (weight applies the 4?/(N) factor so we get an unbiased integral)
	for (uint i = 0; i < 9; ++i) {
		float3 contrib = L * sh[i] * weight;
		InterlockedAdd(g_SH[i].x, contrib.x);
		InterlockedAdd(g_SH[i].y, contrib.y);
		InterlockedAdd(g_SH[i].z, contrib.z);
	}
}