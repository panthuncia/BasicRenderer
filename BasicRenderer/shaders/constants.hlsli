#ifndef __CONSTANTS_HLSLI__
#define __CONSTANTS_HLSLI__

#define PI 3.1415926538

#define SH_FLOAT_SCALE 100 // 1<<20
#define SH_FLOAT_SCALE_INVERSE 1.0f / SH_FLOAT_SCALE

#define MIN_PERCEPTUAL_ROUGHNESS 0.06  // <- used to default to 0.45 but various artifacts can be seen up to 0.08 in fp32 too; 0.06 is a good tradeoff
#define MIN_ROUGHNESS (MIN_PERCEPTUAL_ROUGHNESS*MIN_PERCEPTUAL_ROUGHNESS)

#define MIN_N_DOT_V 1e-4

#endif //__CONSTANTS_HLSLI__