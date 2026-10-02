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

    void Move(float forward, float right, float up);

    void MoveAlongForward(float amount);

    const DirectX::XMMATRIX& GetViewMatrix() const { return m_view; }
    const DirectX::XMMATRIX& GetProjectionMatrix() const { return m_proj; }

    DirectX::XMFLOAT3 GetPosition() const { return m_position; }
    DirectX::XMFLOAT3 GetForward() const;
    DirectX::XMFLOAT3 GetRight() const;

    void SetPosition(DirectX::XMFLOAT3 pos) { m_position = pos; Recalculate(); }
    void SetYaw(float yaw) { m_yaw = yaw; Recalculate(); }
    void SetPitch(float pitch) {
        m_pitch = std::clamp(pitch, -DirectX::XM_PIDIV2 + 0.1f, DirectX::XM_PIDIV2 - 0.1f);
        Recalculate();
    }

    DirectX::XMFLOAT3 GetTarget() const {
        DirectX::XMFLOAT3 f = GetForward();
        DirectX::XMFLOAT3 t;
        t.x = m_position.x + f.x;
        t.y = m_position.y + f.y;
        t.z = m_position.z + f.z;
        return t;
    }

    float GetDistance() const { return 0.0f; }   

private:
    void Recalculate();

    DirectX::XMFLOAT3 m_position = { 0.0f, 400.0f, 1200.0f };
    float m_yaw = 0.0f;                
    float m_pitch = -0.3f;              
    float m_rotateSpeed = 0.0025f;
    float m_minPitch = -DirectX::XM_PIDIV2 + 0.1f;
    float m_maxPitch = DirectX::XM_PIDIV2 - 0.1f;

    DirectX::XMMATRIX m_view;
    DirectX::XMMATRIX m_proj;

    float m_distance = 800.0f;
    float m_minDistance = 1.0f;
    float m_maxDistance = 5000.0f;
    float m_zoomSpeed = 10.0f;
    DirectX::XMFLOAT3 m_target = { 0.0f, 150.0f, 0.0f };
};