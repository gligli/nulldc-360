/* 
 * File:   Vec.h
 * Author: cc
 *
 * Created on 6 septembre 2011, 14:30
 */

#ifndef VEC_H
#define	VEC_H

typedef struct vector4
{
    union
    {
        struct
        {
            float x;
            float y;
            float z;
            float w;
        };
        float f[4];
        unsigned int u[4];
    };
} vector4;

typedef struct vector3
{
    union
    {
        struct
        {
            float x;
            float y;
            float z;
        };
        float f[3];
        unsigned int u[3];
    };
} vector3;

typedef struct vector2
{
    union
    {
        struct
        {
            float x;
            float y;
        };
        float f[2];
        unsigned int u[2];
    };
} vector2;

typedef struct matrix4x4
{
    union
    {
        struct
        {
            float m1_1, m1_2, m1_3, m1_4;
            float m2_1, m2_2, m2_3, m2_4;
            float m3_1, m3_2, m3_3, m3_4;
            float m4_1, m4_2, m4_3, m4_4;
        };
        
        vector4 v[4];
        float f[4][4];
    };
} matrix4x4;


//vector.cpp
/*
 * Operation Vector2
 */
vector2 * vector2Add(vector2 *out, vector2 * v1, vector2 *v2);
vector2 * vector2Sub(vector2 *out, vector2 * v1, vector2 *v2);
vector2 * vector2Mul(vector2 *out, vector2 * v1, vector2 *v2);
vector2 * vector2Scale(vector2 *v1, float scale);
/*
 * Operation Vector3
 */
vector3 * vector3Add(vector3 *out, vector3 * v1, vector3 *v2);
vector3 * vector3Sub(vector3 *out, vector3 * v1, vector3 *v2);
vector3 * vector3Mul(vector3 *out, vector3 * v1, vector3 *v2);
vector3 * vector3Scale(vector3 *v1, float scale);
float vector3Length(vector3 *pV);
vector3 * vector3Normalize(vector3 *pout, vector3 *pv);
vector3 * vector3Cross(vector3 *pout, vector3 *v1, vector3 *v2);
float vector3Dot(vector3 *v1, vector3 *v2);
/*
 * Operation Vector4
 */
vector4 * vector4Add(vector4 *out, vector4 * v1, vector4 *v2);
vector4 * vector4Sub(vector4 *out, vector4 * v1, vector4 *v2);
vector4 * vector4Mul(vector4 *out, vector4 * v1, vector4 *v2);
vector4 * vector4Scale(vector4 *v1, float scale);

// Matrix

/*
 * Create an identity matrix
 */
matrix4x4 * matrixLoadIdentity(matrix4x4 * pout);
/*
 * Create a matrix for translation
 */
matrix4x4 * matrixTranslation(matrix4x4 * m, float x, float y, float z);
/*
 * Create a matrix for scaling
 */
matrix4x4 * matrixScaling(matrix4x4 * m, float x, float y, float z);
/*
 * Create a matrix for rotation
 */
matrix4x4 * matrixRotationX(matrix4x4 * m, float angle);
matrix4x4 * matrixRotationY(matrix4x4 * m, float angle);
matrix4x4 * matrixRotationZ(matrix4x4 * m, float angle);
/*
 * mul 2 matrix
 */
matrix4x4 * matrixMultiply(matrix4x4 * m, const matrix4x4 * a, const matrix4x4 * b);
/*
 * transpose
 */
matrix4x4 * matrixTranspose(matrix4x4 * pout, const matrix4x4 * pm);

/*
 * Projection 
 */
matrix4x4 * matrixOrthoLH(matrix4x4 * pout, float w, float h, float zn, float zf);
matrix4x4 * matrixOrthoRH(matrix4x4 * pout, float w, float h, float zn, float zf);
matrix4x4 * matrixPerspectiveLH(matrix4x4 * pout, float w, float h, float zn, float zf);
matrix4x4 * matrixPerspectiveRH(matrix4x4 * pout, float w, float h, float zn, float zf);
matrix4x4 * matrixPerspectiveFovLH(matrix4x4 * pout, float fovy, float aspect, float zn, float zf);
matrix4x4 * matrixPerspectiveFovRH(matrix4x4 * pout, float fovy, float aspect, float zn, float zf);
matrix4x4 * matrixLookAtLH(matrix4x4 * pout, vector3 * eye, vector3 * at, vector3 * up);
matrix4x4 * matrixLookAtRH(matrix4x4 * pout, vector3 * eye, vector3 * at, vector3 * up);

#endif	/* VEC_H */

