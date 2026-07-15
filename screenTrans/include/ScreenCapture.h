#pragma once

#include <windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <vector>
#include <wrl/client.h>

#include "ST_API.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

namespace ST {

    class ST_API ScreenCapture {
    public:
        bool Initialize(int monitorIndex = 0);
        bool CaptureFrame();
        const std::vector<uint8_t>& GetBuffer() const { return m_pixelBuffer; }
        UINT Width() const { return m_width; }
        UINT Height() const { return m_height; }
        UINT GetBufferSize() const { return m_width * m_height * 4; }

    private:
        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_context;
        ComPtr<IDXGIOutputDuplication> m_duplication;
        ComPtr<ID3D11Texture2D> m_stagingTexture;
        std::vector<uint8_t> m_pixelBuffer;
        UINT m_width = 0;
        UINT m_height = 0;
    };

}