RWStructuredBuffer<float> result : register(u0);
[shader("raygeneration")]
void RayGen() { result[0] = 7.0; }
