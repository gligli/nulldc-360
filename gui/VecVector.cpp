/*
 * File:   Vec.cpp
 * Author: cc
 *
 * Created on 6 septembre 2011, 17:08
 */

#include "Vec.h"
#include "math.h"
/*
 * Operation Vector2
 */
vector2 * vector2Add(vector2 *out, vector2 * v1, vector2 *v2) {
    out->x = v1->x + v2->x;
    out->y = v1->y + v2->y;
    return out;
}

vector2 * vector2Sub(vector2 *out, vector2 * v1, vector2 *v2) {
    out->x = v1->x - v2->x;
    out->y = v1->y - v2->y;
    return out;
}

vector2 * vector2Mul(vector2 *out, vector2 * v1, vector2 *v2) {
    out->x = v1->x * v2->x;
    out->y = v1->y * v2->y;
    return out;
}

vector2 * vector2Scale(vector2 *v1, float scale) {
    v1->x = v1->x * scale;
    v1->y = v1->y * scale;
    return v1;
}

/*
 * Operation Vector3
 */
vector3 * vector3Add(vector3 *out, vector3 * v1, vector3 *v2) {
    out->x = v1->x + v2->x;
    out->y = v1->y + v2->y;
    out->z = v1->z + v2->z;
    return out;
}

vector3 * vector3Sub(vector3 *out, vector3 * v1, vector3 *v2) {
    out->x = v1->x - v2->x;
    out->y = v1->y - v2->y;
    out->z = v1->z - v2->z;
    return out;
}

vector3 * vector3Mul(vector3 *out, vector3 * v1, vector3 *v2) {
    out->x = v1->x * v2->x;
    out->y = v1->y * v2->y;
    out->z = v1->z * v2->z;
    return out;
}

vector3 * vector3Scale(vector3 *v1, float scale) {
    v1->x = v1->x * scale;
    v1->y = v1->y * scale;
    v1->z = v1->z * scale;
    return v1;
}

float vector3Length(vector3 *pV) {
    return (float) sqrt(pV->x * pV->x + pV->y * pV->y + pV->z * pV->z);
}

vector3 * vector3Normalize(vector3 *pout, vector3 *pv) {
    float norm;

    norm = vector3Length(pv);
    if (!norm) {
        pout->x = 0.0f;
        pout->y = 0.0f;
        pout->z = 0.0f;
    } else {
        pout->x = pv->x / norm;
        pout->y = pv->y / norm;
        pout->z = pv->z / norm;
    }
    return pout;
}

vector3 * vector3Cross(vector3 *pout, vector3 *v1, vector3 *v2) {
    vector3 v;

    v.x = v1->y * v2->z - v1->z * v2->y;
    v.y = v1->z * v2->x - v1->x * v2->z;
    v.z = v1->x * v2->y - v1->y * v2->x;
    *pout = v;
    return pout;
}

float vector3Dot(vector3 *v1, vector3 *v2) {
    return v1->x * v2->x + v1->y * v2->y + v1->z * v2->z;
}

/*
 * Operation Vector4
 */
vector4 * vector4Add(vector4 *out, vector4 * v1, vector4 *v2) {
    out->x = v1->x + v2->x;
    out->y = v1->y + v2->y;
    out->z = v1->z + v2->z;
    out->w = v1->w + v2->w;
    return out;
}

vector4 * vector4Sub(vector4 *out, vector4 * v1, vector4 *v2) {
    out->x = v1->x - v2->x;
    out->y = v1->y - v2->y;
    out->z = v1->z - v2->z;
    out->w = v1->w - v2->w;
    return out;
}

vector4 * vector4Mul(vector4 *out, vector4 * v1, vector4 *v2) {
    out->x = v1->x * v2->x;
    out->y = v1->y * v2->y;
    out->z = v1->z * v2->z;
    out->w = v1->w * v2->w;
    return out;
}

vector4 * vector4Scale(vector4 *v1, float scale) {
    v1->x = v1->x * scale;
    v1->y = v1->y * scale;
    v1->z = v1->z * scale;
    v1->w = v1->w * scale;
    return v1;
}