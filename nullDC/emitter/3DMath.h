/**
 * glN64_GX - 3DMath.h
 * Copyright (C) 2003 Orkin
 * Copyright (C) 2008, 2009 sepp256 (Port to Wii/Gamecube/PS3)
 * Copyright (C) 2009 tehpola (paired single optimization for GEKKO)
 *
 * glN64 homepage: http://gln64.emulation64.com
 * Wii64 homepage: http://www.emulatemii.com
 * email address: sepp256@gmail.com
 *
**/

/* parts taken from http://freevec.org/ */

#ifndef _3DMATH_H
#define _3DMATH_H

#include <string.h>
#include <math.h>
#include <altivec.h>

#define USE_ALTIVEC

#ifdef USE_ALTIVEC
	#define ALIGNED16 __attribute__((aligned(16)))
#else
	#define ALIGNED16
#endif

typedef float Mat44[4][4] ALIGNED16;
typedef float Vec4f[4] ALIGNED16;
typedef float Vec3f[3] ALIGNED16;

#define LOAD_ALIGNED_VECTOR(vr, vs)                     \
{                                                       \
        vr = vec_ld(0, (float *)vs);                    \
}

#define STORE_ALIGNED_VECTOR(vr, vs)                    \
{                                                       \
        vec_st(vr,  0, (float *)vs);                    \
}
#define LOAD_ALIGNED_MATRIX(m, vm1, vm2, vm3, vm4)  \
{                                                   \
        vm1 = vec_ld(0,  (float *)m);               \
        vm2 = vec_ld(16, (float *)m);               \
        vm3 = vec_ld(32, (float *)m);               \
        vm4 = vec_ld(48, (float *)m);               \
}

#define STORE_ALIGNED_MATRIX(m, vm1, vm2, vm3, vm4)  \
{                                                    \
        vec_st(vm1,  0, (float *)m);                 \
        vec_st(vm2, 16, (float *)m);                 \
        vec_st(vm3, 32, (float *)m);                 \
        vec_st(vm4, 48, (float *)m);                 \
}

#ifdef USE_ALTIVEC
#if 0
inline void Mat44TransformVector(Vec4f v,Mat44 m)
{
		vector float zero;
		vector float vo,vv,m1,m2,m3,m4;

		LOAD_ALIGNED_MATRIX(m,m1,m2,m3,m4);
		LOAD_ALIGNED_VECTOR(vv,v);

        zero = (vector float) vec_splat_u32(0);

		vo = vec_madd( vec_splat( vv, 0 ), m1, zero );
		vo = vec_madd( vec_splat( vv, 1 ), m2, vo );
		vo = vec_madd( vec_splat( vv, 2 ), m3, vo );
		vo = vec_madd( vec_splat( vv, 3 ), m4, vo );

		STORE_ALIGNED_VECTOR(vo,v);
}
#else
inline void Mat44TransformVector(Vec4f vtx,Mat44 mtx)
{
	float x, y, z, w;
	x = vtx[0];
	y = vtx[1];
	z = vtx[2];
	w = vtx[3];

	vtx[0] = x * mtx[0][0] +
	         y * mtx[1][0] +
	         z * mtx[2][0] +
	         w * mtx[3][0];

	vtx[1] = x * mtx[0][1] +
	         y * mtx[1][1] +
	         z * mtx[2][1] +
	         w * mtx[3][1];

	vtx[2] = x * mtx[0][2] +
	         y * mtx[1][2] +
	         z * mtx[2][2] +
	         w * mtx[3][2];

	vtx[3] = x * mtx[0][3] +
	         y * mtx[1][3] +
	         z * mtx[2][3] +
	         w * mtx[3][3];
/*
	vtx[0] += mtx[3][0];
	vtx[1] += mtx[3][1];
	vtx[2] += mtx[3][2];
	vtx[3] += mtx[3][3];*/
}
#endif

#if 0
Vec4f dp_res;
	
inline float DotProduct( Vec4f v0, Vec4f v1 )
{
//	printf("%p %p\n",v0,v1);
	
    asm volatile(
		"lvx 2,0,3						\n"
		"lvx 3,0,4						\n"
		".long 0x142219d0				\n" //vmsum4fp128 %vr1,%vr2,%vr3
		"lis 3,dp_res@h					\n"
		"ori 3,3,dp_res@l				\n"
		"stvx 1,0,3						\n"
	);

	return 0;
}
#else
inline float DotProduct( Vec4f v0, Vec4f v1 )
{
	float	dot;
	dot = v0[0]*v1[0] + v0[1]*v1[1] + v0[2]*v1[2] + v0[3]*v1[3];
	return dot;
}
#endif

#else
inline void Mat44TransformVector(Vec4f vtx,Mat44 mtx)
{
	float x, y, z, w;
	x = vtx[0];
	y = vtx[1];
	z = vtx[2];
	w = vtx[3];

	vtx[0] = x * mtx[0][0] +
	         y * mtx[1][0] +
	         z * mtx[2][0] +
	         w * mtx[3][0];

	vtx[1] = x * mtx[0][1] +
	         y * mtx[1][1] +
	         z * mtx[2][1] +
	         w * mtx[3][1];

	vtx[2] = x * mtx[0][2] +
	         y * mtx[1][2] +
	         z * mtx[2][2] +
	         w * mtx[3][2];

	vtx[3] = x * mtx[0][3] +
	         y * mtx[1][3] +
	         z * mtx[2][3] +
	         w * mtx[3][3];
/*
	vtx[0] += mtx[3][0];
	vtx[1] += mtx[3][1];
	vtx[2] += mtx[3][2];
	vtx[3] += mtx[3][3];*/
}

inline float DotProduct( Vec4f v0, Vec4f v1 )
{
	float	dot;
	dot = v0[0]*v1[0] + v0[1]*v1[1] + v0[2]*v1[2] + v0[3]*v1[3];
	return dot;
}

#endif
#endif
