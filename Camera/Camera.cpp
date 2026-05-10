#include "Camera.h"
#include <algorithm>

using namespace DirectX;

Camera::Camera() {
    Recalculate();
}

void Camera::Update(float aspectRatio) {
    m_proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspectRatio, 0.1f, 100.0f);
    Recalculate();
}

void Camera::Rotate(int dx, int dy) {
    m_yaw += dx * m_rotateSpeed;
    m_pitch += dy * m_rotateSpeed;
    m_pitch = std::clamp(m_pitch, -XM_PIDIV2 + 0.1f, XM_PIDIV2 - 0.1f);
    Recalculate();
}

void Camera::Zoom(float amount) {
    m_distance -= amount * m_zoomSpeed;
    m_distance = std::clamp(m_distance, m_minDistance, m_maxDistance);
    Recalculate();
}

void Camera::Reset() {
    m_distance = 5.0f;
    m_yaw = 0.0f;
    m_pitch = 0.2f;
    Recalculate();
}

XMFLOAT3 Camera::GetPosition() const {
    float x = m_distance * sinf(m_yaw) * cosf(m_pitch);
    float y = m_distance * sinf(m_pitch) + 2.0f; // +2 высота над моделью
    float z = m_distance * cosf(m_yaw) * cosf(m_pitch);
    return XMFLOAT3(x, y, z);
}

void Camera::Recalculate() {
    XMVECTOR pos = XMLoadFloat3(&GetPosition());
    XMVECTOR target = XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    m_view = XMMatrixLookAtLH(pos, target, up);
}