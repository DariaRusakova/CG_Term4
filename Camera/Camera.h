#pragma once
#include <DirectXMath.h>
#include <algorithm>

class Camera {
public:
    Camera();

    void Update(float aspectRatio);
    void Rotate(int dx, int dy);
    void Zoom(float amount);
    void Reset();

    const DirectX::XMMATRIX& GetViewMatrix() const { return m_view; }
    const DirectX::XMMATRIX& GetProjectionMatrix() const { return m_proj; }
    DirectX::XMFLOAT3 GetPosition() const;
    float GetDistance() const { return m_distance; }

    DirectX::XMFLOAT3 GetForward() const;

    void SetDistance(float dist) { m_distance = std::clamp(dist, m_minDistance, m_maxDistance); }
    void SetYaw(float yaw) { m_yaw = yaw; }
    void SetPitch(float pitch) { m_pitch = std::clamp(pitch, -DirectX::XM_PIDIV2 + 0.1f, DirectX::XM_PIDIV2 - 0.1f); }
    void ZoomScaled(float factor) {
        m_distance *= factor;
        m_distance = std::clamp(m_distance, m_minDistance, m_maxDistance);
        Recalculate();
    }

    void MoveTarget(float dx, float dy, float dz) {
        m_target.x += dx;
        m_target.y += dy;
        m_target.z += dz;
        Recalculate();
    }
    DirectX::XMFLOAT3 m_target = { 0.0f, 150.0f, 0.0f };
    DirectX::XMFLOAT3 GetTarget() const { return m_target; }
    
private:
    void Recalculate();

    float m_distance = 5.0f;
    float m_yaw = 0.0f;
    float m_pitch = 0.2f;
    float m_minDistance = 1.0f;
    float m_maxDistance = 5000.0f;
    float m_rotateSpeed = 0.002f;
    float m_zoomSpeed = 10.0f;

    DirectX::XMMATRIX m_view;
    DirectX::XMMATRIX m_proj;

};