/* 
 * File:   Vec.cpp
 * Author: cc
 * 
 * Created on 6 septembre 2011, 14:30
 */

#include "Vec.h"
#include <math.h>

//http://www.koders.com/cpp/fid1FB3018EE58694DE4403FDDAD1BE651F6CEF8E42.aspx
//http://source.winehq.org/source/include/d3dx8math.h?v=wine-1.1.6#L99
//http://source.winehq.org/source/dlls/d3dx8/math.c?v=wine-1.1.6#L558

/*
 * Create an identity matrix
 */
matrix4x4 * matrixLoadIdentity(matrix4x4 * pout) {
    pout->f[0][1] = pout->f[0][2] = pout->f[0][3] =
            pout->f[1][0] = pout->f[1][2] = pout->f[1][3] =
            pout->f[2][0] = pout->f[2][1] = pout->f[2][3] =
            pout->f[3][0] = pout->f[3][1] = pout->f[3][2] = 0;
    pout->f[0][0] = pout->f[1][1] = pout->f[2][2] = pout->f[3][3] = 1.f;
    return pout;
}

/*
 * Create a matrix for translation
 */
matrix4x4 * matrixTranslation(matrix4x4 * m, float x, float y, float z) {
    matrixLoadIdentity(m);
    m->f[3][0] = x;
    m->f[3][1] = y;
    m->f[3][2] = z;
    return m;
}

/*
 * Create a matrix for scaling
 */
matrix4x4 * matrixScaling(matrix4x4 * m, float x, float y, float z) {
    matrixLoadIdentity(m);
    m->f[0][0] = x;
    m->f[1][1] = y;
    m->f[2][2] = z;
    return m;
}

/*
 * Create a matrix for rotation
 */
matrix4x4 * matrixRotationX(matrix4x4 * m, float angle) {
    matrixLoadIdentity(m);
    m->f[1][1] = cos(angle);
    m->f[2][2] = cos(angle);
    m->f[1][2] = sin(angle);
    m->f[2][1] = -sin(angle);
    return m;
}

matrix4x4 * matrixRotationY(matrix4x4 * m, float angle) {
    matrixLoadIdentity(m);
    m->f[0][0] = cos(angle);
    m->f[2][2] = cos(angle);
    m->f[0][2] = -sin(angle);
    m->f[2][0] = sin(angle);
    return m;
}

matrix4x4 * matrixRotationZ(matrix4x4 * m, float angle) {
    matrixLoadIdentity(m);
    m->f[0][0] = cos(angle);
    m->f[1][1] = cos(angle);
    m->f[0][1] = sin(angle);
    m->f[1][0] = -sin(angle);
    return m;
}

/*
 * mul 2 matrix
 */
matrix4x4 * matrixMultiply(matrix4x4 * m, const matrix4x4 * a, const matrix4x4 * b) {
    matrix4x4 d;
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            d.f[i][j] = a->f[i][0] * b->f[0][j] + a->f[i][1] * b->f[1][j] + a->f[i][2] * b->f[2][j] + a->f[i][3] * b->f[3][j];
        }
    }
    *m = d;
    return m;
}

/*
 * transpose
 */
matrix4x4 * matrixTranspose(matrix4x4 * pout, const matrix4x4 * pm) {
    const matrix4x4 m = *pm;
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            pout->f[i][j] = m.f[j][i];
        }
    }
    return pout;
}

/*
 * Projection 
 */
matrix4x4 * matrixOrthoLH(matrix4x4 * pout, float w, float h, float zn, float zf) {
    matrixLoadIdentity(pout);
    pout->f[0][0] = 2.0f / w;
    pout->f[1][1] = 2.0f / h;
    pout->f[2][2] = 1.0f / (zf - zn);
    pout->f[3][2] = zn / (zn - zf);
    return pout;
}

matrix4x4 * matrixOrthoRH(matrix4x4 * pout, float w, float h, float zn, float zf) {
    matrixLoadIdentity(pout);
    pout->f[0][0] = 2.0f / w;
    pout->f[1][1] = 2.0f / h;
    pout->f[2][2] = 1.0f / (zn - zf);
    pout->f[3][2] = zn / (zn - zf);
    return pout;
}

matrix4x4 * matrixPerspectiveLH(matrix4x4 * pout, float w, float h, float zn, float zf) {
    matrixLoadIdentity(pout);
    pout->f[0][0] = 2.0f * zn / w;
    pout->f[1][1] = 2.0f * zn / h;
    pout->f[2][2] = zf / (zf - zn);
    pout->f[3][2] = (zn * zf) / (zn - zf);
    pout->f[2][3] = 1.0f;
    pout->f[3][3] = 0.0f;
    return pout;
}

matrix4x4 * matrixPerspectiveRH(matrix4x4 * pout, float w, float h, float zn, float zf) {
    matrixLoadIdentity(pout);
    pout->f[0][0] = 2.0f * zn / w;
    pout->f[1][1] = 2.0f * zn / h;
    pout->f[2][2] = zf / (zn - zf);
    pout->f[3][2] = (zn * zf) / (zn - zf);
    pout->f[2][3] = -1.0f;
    pout->f[3][3] = 0.0f;
    return pout;
}

matrix4x4 * matrixPerspectiveFovLH(matrix4x4 * pout, float fovy, float aspect, float zn, float zf) {
    matrixLoadIdentity(pout);
    pout->f[0][0] = 1.0f / (aspect * tan(fovy / 2.0f));
    pout->f[1][1] = 1.0f / tan(fovy / 2.0f);
    pout->f[2][2] = zf / (zf - zn);
    pout->f[2][3] = 1.0f;
    pout->f[3][2] = (zf * zn) / (zn - zf);
    pout->f[3][3] = 0.0f;
    return pout;
}

matrix4x4 * matrixPerspectiveFovRH(matrix4x4 * pout, float fovy, float aspect, float zn, float zf) {
    matrixLoadIdentity(pout);
    pout->f[0][0] = 1.0f / (aspect * tan(fovy / 2.0f));
    pout->f[1][1] = 1.0f / tan(fovy / 2.0f);
    pout->f[2][2] = zf / (zn - zf);
    pout->f[2][3] = -1.0f;
    pout->f[3][2] = (zf * zn) / (zn - zf);
    pout->f[3][3] = 0.0f;
    return pout;
}

matrix4x4 * matrixLookAtLH(matrix4x4 * pout, vector3 * eye, vector3 * at, vector3 * _up) {
    vector3 right, rightn, up, upn, vec, vec2;

    vector3Sub(&vec2, at, eye);
    vector3Normalize(&vec, &vec2);
    vector3Cross(&right, _up, &vec);
    vector3Cross(&up, &vec, &right);
    vector3Normalize(&rightn, &right);
    vector3Normalize(&upn, &up);

    pout->f[0][0] = rightn.x;
    pout->f[1][0] = rightn.y;
    pout->f[2][0] = rightn.z;
    pout->f[3][0] = -vector3Dot(&rightn, eye);
    pout->f[0][1] = upn.x;
    pout->f[1][1] = upn.y;
    pout->f[2][1] = upn.z;
    pout->f[3][1] = -vector3Dot(&upn, eye);
    pout->f[0][2] = vec.x;
    pout->f[1][2] = vec.y;
    pout->f[2][2] = vec.z;
    pout->f[3][2] = -vector3Dot(&vec, eye);
    pout->f[0][3] = 0.0f;
    pout->f[1][3] = 0.0f;
    pout->f[2][3] = 0.0f;
    pout->f[3][3] = 1.0f;
    return pout;
}

matrix4x4 * matrixLookAtRH(matrix4x4 * pout, vector3 * eye, vector3 * at, vector3 * _up) {
    vector3 right, rightn, up, upn, vec, vec2;

    vector3Sub(&vec2, at, eye);
    vector3Normalize(&vec, &vec2);
    vector3Cross(&right, _up, &vec);
    vector3Cross(&up, &vec, &right);
    vector3Normalize(&rightn, &right);
    vector3Normalize(&upn, &up);

    pout->f[0][0] = -rightn.x;
    pout->f[1][0] = -rightn.y;
    pout->f[2][0] = -rightn.z;
    pout->f[3][0] = vector3Dot(&rightn, eye);
    pout->f[0][1] = upn.x;
    pout->f[1][1] = upn.y;
    pout->f[2][1] = upn.z;
    pout->f[3][1] = -vector3Dot(&upn, eye);
    pout->f[0][2] = vec.x;
    pout->f[1][2] = vec.y;
    pout->f[2][2] = vec.z;
    pout->f[3][2] = vector3Dot(&vec, eye);
    pout->f[0][3] = 0.0f;
    pout->f[1][3] = 0.0f;
    pout->f[2][3] = 0.0f;
    pout->f[3][3] = 1.0f;
    return pout;
}