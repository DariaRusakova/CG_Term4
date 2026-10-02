#include "Camera.h"
#include <algorithm>

using namespace DirectX;

Camera::Camera() {
    Recalculate();
}

void Camera::Update(float aspectRatio) {
    m_proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspectRatio, 0.1f, 10000.0f);
    Recalculate();
}

void Camera::Rotate(int dx, int dy) {
    m_yaw += dx * m_rotateSpeed;
    m_pitch -= dy * m_rotateSpeed;  
    m_pitch = std::clamp(m_pitch, m_minPitch, m_maxPitch);
    Recalculate();
}

void Camera::Zoom(float ) {

}

void Camera::Reset() {
    m_position = { 0.0f, 800.0f, 1800.0f };
    m_yaw = XM_PI;
    m_pitch = -0.35f;
    Recalculate();
}

XMFLOAT3 Camera::GetForward() const {
    float cp = cosf(m_pitch);
    float sp = sinf(m_pitch);
    float cy = cosf(m_yaw);
    float sy = sinf(m_yaw);

    XMFLOAT3 fwd;
    fwd.x = -sy * cp;  
    fwd.y = sp;        
    fwd.z = -cy * cp;  
    return fwd;
}

XMFLOAT3 Camera::GetRight() const {

    XMFLOAT3 r;
    r.x = cosf(m_yaw);
    r.y = 0.0f;
    r.z = -sinf(m_yaw);
    return r;
}

void Camera::Move(float forward, float right, float up) {
    XMFLOAT3 fwd = GetForward();
    XMFLOAT3 rgt = GetRight();

    XMFLOAT3 fwdHoriz = { fwd.x, 0.0f, fwd.z };
    float lenF = sqrtf(fwdHoriz.x * fwdHoriz.x + fwdHoriz.z * fwdHoriz.z);
    if (lenF > 1e-6f) {
        fwdHoriz.x /= lenF;
        fwdHoriz.z /= lenF;
    }
    else {
        fwdHoriz = { 0.0f, 0.0f, 1.0f };
    }

    m_position.x += fwdHoriz.x * forward + rgt.x * right;
    m_position.y += up;
    m_position.z += fwdHoriz.z * forward + rgt.z * right;

    Recalculate();
}

void Camera::MoveAlongForward(float amount) {
    XMFLOAT3 fwd = GetForward();
    m_position.x += fwd.x * amount;
    m_position.y += fwd.y * amount;
    m_position.z += fwd.z * amount;
    Recalculate();
}

void Camera::Recalculate() {
    XMVECTOR pos = XMLoadFloat3(&m_position);
    XMFLOAT3 f3 = GetForward();
    XMVECTOR fwd = XMLoadFloat3(&f3);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    m_view = XMMatrixLookToLH(pos, fwd, up);
}