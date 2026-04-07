#ifndef __ROOT_CONSTANT_ENCODING_H__
#define __ROOT_CONSTANT_ENCODING_H__

#ifdef __cplusplus
#define ROOT_CONSTANT_AS_FLOAT(rootConstant) (rootConstant)
#else
#define ROOT_CONSTANT_AS_FLOAT(rootConstant) asfloat(rootConstant)
#endif

#endif // __ROOT_CONSTANT_ENCODING_H__