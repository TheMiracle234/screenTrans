#include "ScreenCapture.h"

#include <iostream>

namespace ST {

    ///////////////////////////////////////////////////////////////
    // Initialize
    ///////////////////////////////////////////////////////////////

    bool ScreenCapture::Initialize(int monitorIndex)
    {
        ///////////////////////////////////////////////////////////
        // 创建设备
        ///////////////////////////////////////////////////////////

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        D3D_FEATURE_LEVEL levels[] =
        {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1
        };

        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            levels,
            2,
            D3D11_SDK_VERSION,
            &m_device,
            nullptr,
            &m_context
        );

        if (FAILED(hr)) {
            std::cout << "D3D11CreateDevice failed\n";
            return false;
        }

        ///////////////////////////////////////////////////////////
        // 获取 adapter
        ///////////////////////////////////////////////////////////

        ComPtr<IDXGIDevice> dxgiDevice;

        if (FAILED(m_device.As(&dxgiDevice)))
            return false;

        ComPtr<IDXGIAdapter> adapter;

        if (FAILED(dxgiDevice->GetParent(
            IID_PPV_ARGS(&adapter))))
        {
            return false;
        }

        ///////////////////////////////////////////////////////////
        // 获取 output
        ///////////////////////////////////////////////////////////

        ComPtr<IDXGIOutput> output;

        if (FAILED(adapter->EnumOutputs(
            monitorIndex,
            &output)))
        {
            return false;
        }

        ComPtr<IDXGIOutput1> output1;

        if (FAILED(output.As(&output1)))
            return false;

        ///////////////////////////////////////////////////////////
        // 创建 duplication
        ///////////////////////////////////////////////////////////

        hr = output1->DuplicateOutput(
            m_device.Get(),
            &m_duplication
        );

        if (FAILED(hr)) {
            std::cout << "DuplicateOutput failed\n";
            return false;
        }

        ///////////////////////////////////////////////////////////
        // 获取屏幕尺寸
        ///////////////////////////////////////////////////////////

        DXGI_OUTDUPL_DESC desc;
        m_duplication->GetDesc(&desc);
        m_width = desc.ModeDesc.Width;
        m_height = desc.ModeDesc.Height;

        ///////////////////////////////////////////////////////////
        // 创建 staging texture
        ///////////////////////////////////////////////////////////

        D3D11_TEXTURE2D_DESC texDesc = {};

        texDesc.Width = m_width;
        texDesc.Height = m_height;
        texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.ArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_STAGING;
        texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        hr = m_device->CreateTexture2D(
            &texDesc,
            nullptr,
            &m_stagingTexture
        );

        if (FAILED(hr)) {
            std::cout << "CreateTexture2D failed\n";
            return false;
        }

        ///////////////////////////////////////////////////////////
        // 提前分配 CPU buffer
        ///////////////////////////////////////////////////////////

        m_pixelBuffer.resize(
            size_t(m_width) * m_height * 4
        );

        return true;
    }

    ///////////////////////////////////////////////////////////////
    // CaptureFrame
    ///////////////////////////////////////////////////////////////

    bool ScreenCapture::CaptureFrame()
    {
        ///////////////////////////////////////////////////////////
        // 获取下一帧
        ///////////////////////////////////////////////////////////

        ComPtr<IDXGIResource> resource;

        DXGI_OUTDUPL_FRAME_INFO frameInfo;

        HRESULT hr =
            m_duplication->AcquireNextFrame(
                16,
                &frameInfo,
                &resource
            );

        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
            return false;

        if (FAILED(hr))
            return false;

        ///////////////////////////////////////////////////////////
        // RAII ReleaseFrame
        ///////////////////////////////////////////////////////////

        struct FrameGuard {
            IDXGIOutputDuplication* dupl;
            ~FrameGuard()
            {
                if (dupl)
                    dupl->ReleaseFrame();
            }
        } guard{ m_duplication.Get() };

        ///////////////////////////////////////////////////////////
        // 获取 GPU texture
        ///////////////////////////////////////////////////////////

        ComPtr<ID3D11Texture2D> gpuTexture;

        if (FAILED(resource.As(&gpuTexture)))
            return false;

        ///////////////////////////////////////////////////////////
        // GPU -> staging
        ///////////////////////////////////////////////////////////

        m_context->CopyResource(
            m_stagingTexture.Get(),
            gpuTexture.Get()
        );

        ///////////////////////////////////////////////////////////
        // Map
        ///////////////////////////////////////////////////////////

        D3D11_MAPPED_SUBRESOURCE mapped;

        hr = m_context->Map(
            m_stagingTexture.Get(),
            0,
            D3D11_MAP_READ,
            0,
            &mapped
        );

        if (FAILED(hr))
            return false;

        ///////////////////////////////////////////////////////////
        // 拷贝数据
        ///////////////////////////////////////////////////////////

        uint8_t* dst = m_pixelBuffer.data();

        uint8_t* src =
            static_cast<uint8_t*>(mapped.pData);

        const UINT rowBytes = m_width * 4;

        for (UINT y = 0; y < m_height; y++) {
            memcpy(dst, src, rowBytes);
            dst += rowBytes;
            src += mapped.RowPitch;
        }

        ///////////////////////////////////////////////////////////
        // Unmap
        ///////////////////////////////////////////////////////////

        m_context->Unmap(
            m_stagingTexture.Get(),
            0
        );

        return true;
    }

}