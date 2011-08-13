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
#include "../main/main.h"

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
inline void Mat44Copy(Mat44 dst, Mat44 src)
{
        vector float v1, v2, v3, v4;
        LOAD_ALIGNED_MATRIX(src, v1, v2, v3, v4);
        STORE_ALIGNED_MATRIX(dst, v1, v2, v3, v4);
}

inline void Mat44MulTo(Mat44 m1, Mat44 m2, Mat44 m3)
{
		vector float zero;
        vector float vA1, vA2, vA3, vA4, vB1, vB2, vB3, vB4;
        vector float vC1, vC2, vC3, vC4;
 
        // Load matrices and multiply the first row while we wait for the next row
        zero = (vector float) vec_splat_u32(0);
 
        LOAD_ALIGNED_MATRIX(m3, vA1, vA2, vA3, vA4);
        LOAD_ALIGNED_MATRIX(m2, vB1, vB2, vB3, vB4);
 
        // Calculate the first column of m1
        vC1 = vec_madd( vec_splat( vA1, 0 ), vB1, zero );
        vC2 = vec_madd( vec_splat( vA2, 0 ), vB1, zero );
        vC3 = vec_madd( vec_splat( vA3, 0 ), vB1, zero );
        vC4 = vec_madd( vec_splat( vA4, 0 ), vB1, zero );
 
        // By now we should have loaded both matrices and be done with the first row
        // Multiply vA x vB2, add to previous results, vC
        vC1 = vec_madd( vec_splat( vA1, 1 ), vB2, vC1 );
        vC2 = vec_madd( vec_splat( vA2, 1 ), vB2, vC2 );
        vC3 = vec_madd( vec_splat( vA3, 1 ), vB2, vC3 );
        vC4 = vec_madd( vec_splat( vA4, 1 ), vB2, vC4 );
 
        // Multiply vA x vB3, add to previous results, vC
        vC1 = vec_madd( vec_splat( vA1, 2 ), vB3, vC1 );
        vC2 = vec_madd( vec_splat( vA2, 2 ), vB3, vC2 );
        vC3 = vec_madd( vec_splat( vA3, 2 ), vB3, vC3 );
        vC4 = vec_madd( vec_splat( vA4, 2 ), vB3, vC4 );
 
        // Multiply vA x vB3, add to previous results, vC
        vC1 = vec_madd( vec_splat( vA1, 3 ), vB4, vC1 );
        vC2 = vec_madd( vec_splat( vA2, 3 ), vB4, vC2 );
        vC3 = vec_madd( vec_splat( vA3, 3 ), vB4, vC3 );
        vC4 = vec_madd( vec_splat( vA4, 3 ), vB4, vC4 );
 
        // Store back the result
        STORE_ALIGNED_MATRIX(m1, vC1, vC2, vC3, vC4);
}

inline void Mat44Transp(Mat44 m)
{
        vector float vm_1, vm_2, vm_3, vm_4,
                     vr_1, vr_2, vr_3, vr_4;
        // Load matrix
        LOAD_ALIGNED_MATRIX(m, vm_1, vm_2, vm_3, vm_4);
 
        // Do the transpose, first set of moves
        vr_1 = vec_mergeh(vm_1, vm_3);
        vr_2 = vec_mergel(vm_1, vm_3);
        vr_3 = vec_mergeh(vm_2, vm_4);
        vr_4 = vec_mergel(vm_2, vm_4);
        // Get the resulting vectors
        vm_1 = vec_mergeh(vr_1, vr_3);
        vm_2 = vec_mergel(vr_1, vr_3);
        vm_3 = vec_mergeh(vr_2, vr_4);
        vm_4 = vec_mergel(vr_2, vr_4);
 
        // Store back the result
        STORE_ALIGNED_MATRIX(m, vm_1, vm_2, vm_3, vm_4);
}

inline void Mat44TransformVertex(Vec4f v,Mat44 m)
{
		vector float vo,vv,m1,m2,m3,m4;

		LOAD_ALIGNED_MATRIX(m,m1,m2,m3,m4);
		LOAD_ALIGNED_VECTOR(vv,v);

		vo = vec_madd( vec_splat( vv, 0 ), m1, m4 );
		vo = vec_madd( vec_splat( vv, 1 ), m2, vo );
		vo = vec_madd( vec_splat( vv, 2 ), m3, vo );

		STORE_ALIGNED_VECTOR(vo,v);
}

inline void Mat44TransformVector( Vec3f v,Mat44 m )
{
		vector float vo,vv,v1,v2,v3,zero;

        zero = (vector float) vec_splat_u32(0);

		LOAD_ALIGNED_MATRIX(m,v1,v2,v3,vv); //vv is dummy
		LOAD_ALIGNED_VECTOR(vv,v);

		vo = vec_madd( vec_splat( vv, 0 ), v1, zero );
		vo = vec_madd( vec_splat( vv, 1 ), v2, vo );
		vo = vec_madd( vec_splat( vv, 2 ), v3, vo );
		
		STORE_ALIGNED_VECTOR(vo,v);
}
#else
inline void Mat44Copy(Mat44 dst, Mat44 src)
{
	memcpy( dst, src, 16 * sizeof( float ) );
}

inline void Mat44MulTo(Mat44 dst, Mat44 m0, Mat44 m1)
{
	int i;
	float tmp[4][4];

	for (i = 0; i < 4; i++)
	{
		tmp[0][i] = m0[0][i]*m1[0][0] + m0[1][i]*m1[0][1] + m0[2][i]*m1[0][2] + m0[3][i]*m1[0][3];
		tmp[1][i] = m0[0][i]*m1[1][0] + m0[1][i]*m1[1][1] + m0[2][i]*m1[1][2] + m0[3][i]*m1[1][3];
		tmp[2][i] = m0[0][i]*m1[2][0] + m0[1][i]*m1[2][1] + m0[2][i]*m1[2][2] + m0[3][i]*m1[2][3];
		tmp[3][i] = m0[0][i]*m1[3][0] + m0[1][i]*m1[3][1] + m0[2][i]*m1[3][2] + m0[3][i]*m1[3][3];
	}
	Mat44Copy( dst, tmp );
}

inline void Mat44Transp(Mat44 mtx)
{
	float tmp;

	tmp = mtx[0][1];
	mtx[0][1] = mtx[1][0];
	mtx[1][0] = tmp;

	tmp = mtx[0][2];
	mtx[0][2] = mtx[2][0];
	mtx[2][0] = tmp;

	tmp = mtx[1][2];
	mtx[1][2] = mtx[2][1];
	mtx[2][1] = tmp;

	tmp = mtx[0][3];
	mtx[0][3] = mtx[3][0];
	mtx[3][0] = tmp;

	tmp = mtx[1][3];
	mtx[1][3] = mtx[3][1];
	mtx[3][1] = tmp;

	tmp = mtx[2][3];
	mtx[2][3] = mtx[3][2];
	mtx[3][2] = tmp;

}

inline void Mat44TransformVertex(Vec4f vtx,Mat44 mtx)
{
	float x, y, z;
	x = vtx[0];
	y = vtx[1];
	z = vtx[2];

	vtx[0] = x * mtx[0][0] +
	         y * mtx[1][0] +
	         z * mtx[2][0];

	vtx[1] = x * mtx[0][1] +
	         y * mtx[1][1] +
	         z * mtx[2][1];

	vtx[2] = x * mtx[0][2] +
	         y * mtx[1][2] +
	         z * mtx[2][2];

	vtx[3] = x * mtx[0][3] +
	         y * mtx[1][3] +
	         z * mtx[2][3];

	vtx[0] += mtx[3][0];
	vtx[1] += mtx[3][1];
	vtx[2] += mtx[3][2];
	vtx[3] += mtx[3][3];
}

inline void Mat44TransformVector( Vec3f vec,Mat44 mtx)
{
	vec[0] = mtx[0][0] * vec[0]
		   + mtx[1][0] * vec[1]
		   + mtx[2][0] * vec[2];
	vec[1] = mtx[0][1] * vec[0]
		   + mtx[1][1] * vec[1]
		   + mtx[2][1] * vec[2];
	vec[2] = mtx[0][2] * vec[0]
		   + mtx[1][2] * vec[1]
		   + mtx[2][2] * vec[2];
}
#endif


inline void Normalize( float v[3] )
{
	float len;

	len = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
	if (len != 0.0)
	{
		len = 1.0f/sqrtf( len );
		v[0] *= len;
		v[1] *= len;
		v[2] *= len;
	}
}

inline void Normalize2D( float v[2] )
{
	float len;

	len = (float)sqrt( v[0]*v[0] + v[1]*v[1] );
/*	if (len != 0.0)
	{*/
		v[0] /= len;
		v[1] /= len;
/*	}
	else
	{
		v[0] = 0.0;
		v[1] = 0.0;
	}*/
}

inline float DotProduct( float v0[3], float v1[3] )
{
	float	dot;
	dot = v0[0]*v1[0] + v0[1]*v1[1] + v0[2]*v1[2];
	return dot;
}

#endif
