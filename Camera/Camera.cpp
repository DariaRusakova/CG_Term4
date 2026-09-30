#include "Camera.h"
#include <algorithm>

using namespace DirectX;

Camera::Camera() {
    Recalculate();
}

void Camera::Update(float aspectRatio) {
    m_proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspectRatio, 0.1f, 5000.0f);
    Recalculate();
}

void Camera::Rotate(int dx, int dy) {
    m_yaw += dx * m_rotateSpeed;
    m_pitch += dy * m_rotateSpeed;
    m_pitch = std::clamp(m_pitch, 0.05f, XM_PIDIV2 - 0.1f);
    Recalculate();
}

void Camera::Zoom(float amount) {
    m_distance -= amount * m_zoomSpeed;
    m_distance = std::clamp(m_distance, m_minDistance, m_maxDistance);
    Recalculate();
}

void Camera::Reset() {
    m_distance = 800.0f;
    m_yaw = 0.0f;
    m_pitch = 0.6f;
    Recalculate();
}

XMFLOAT3 Camera::GetPosition() const {
    float x = m_distance * sinf(m_yaw) * cosf(m_pitch);
    float y = m_distance * sinf(m_pitch);
    float z = m_distance * cosf(m_yaw) * cosf(m_pitch);

    const float minY = 300.0f;
    if (y < minY) y = minY;

    return XMFLOAT3(x, y, z);
}

void Camera::Recalculate() {
    XMVECTOR pos = XMLoadFloat3(&GetPosition());
    XMVECTOR target = XMLoadFloat3(&m_target);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    m_view = XMMatrixLookAtLH(pos, target, up);
}

XMFLOAT3 Camera::GetForward() const {
    XMFLOAT3 pos = GetPosition();
    XMFLOAT3 tgt = GetTarget();
    XMVECTOR dir = XMLoadFloat3(&tgt) - XMLoadFloat3(&pos);
    XMFLOAT3 out;
    XMStoreFloat3(&out, XMVector3Normalize(dir));
    return out;
}