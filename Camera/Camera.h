#pragma once
#include <DirectXMath.h>

class Camera {
public:
    Camera();

    void Update(float aspectRatio);
    void Rotate(int dx, int dy);
    void Zoom(float amount);
    void Reset();

    DirectX::XMFLOAT3 GetPosition() const;
    DirectX::XMMATRIX GetViewMatrix() const { return m_view; }
    DirectX::XMMATRIX GetProjectionMatrix() const { return m_proj; }

private:
    DirectX::XMMATRIX m_view;
    DirectX::XMMATRIX m_proj;

    float m_distance = 5.0f;
    float m_yaw = 0.0f;
    float m_pitch = 0.2f;

    float m_rotateSpeed = 0.005f;
    float m_zoomSpeed = 0.5f;
    float m_minDistance = 1.0f;
    float m_maxDistance = 30.0f;

    void Recalculate();
};