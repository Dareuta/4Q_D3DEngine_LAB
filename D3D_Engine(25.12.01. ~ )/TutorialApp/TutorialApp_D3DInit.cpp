// InitD3D / UninitD3D
#include "../../D3D_Core/pch.h"
#include "TutorialApp.h"

bool TutorialApp::InitD3D()
{
    HRESULT hr = S_OK;

    // =========================================================================
    // 0) Device / SwapChain
    // =========================================================================
    DXGI_SWAP_CHAIN_DESC swapDesc = {};
    swapDesc.BufferCount = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.OutputWindow = m_hWnd;
    swapDesc.Windowed = true;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferDesc.Width = m_ClientWidth;
    swapDesc.BufferDesc.Height = m_ClientHeight;
    swapDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SampleDesc.Quality = 0;

    UINT creationFlags = 0;
#ifdef _DEBUG
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HR_T(D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        creationFlags, nullptr, 0, D3D11_SDK_VERSION,
        &swapDesc,
        &m_pSwapChain,
        &m_pDevice, nullptr,
        &m_pDeviceContext));

    // =========================================================================
    // 1) BackBuffer RTV
    // =========================================================================
    ID3D11Texture2D* pBackBufferTexture = nullptr;
    HR_T(m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBufferTexture));
    HR_T(m_pDevice->CreateRenderTargetView(pBackBufferTexture, nullptr, &m_pRenderTargetView));
    SAFE_RELEASE(pBackBufferTexture);

    // =========================================================================
    // 2) Depth/Stencil
    // =========================================================================
    D3D11_TEXTURE2D_DESC dsDesc = {};
    dsDesc.Width = m_ClientWidth;
    dsDesc.Height = m_ClientHeight;
    dsDesc.MipLevels = 1;
    dsDesc.ArraySize = 1;
    dsDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsDesc.SampleDesc.Count = 1;
    dsDesc.SampleDesc.Quality = 0;
    dsDesc.Usage = D3D11_USAGE_DEFAULT;
    dsDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    HR_T(m_pDevice->CreateTexture2D(&dsDesc, nullptr, &m_pDepthStencil));
    HR_T(m_pDevice->CreateDepthStencilView(m_pDepthStencil, nullptr, &m_pDepthStencilView));

    // =========================================================================
    // 3) Default depth state
    // =========================================================================
    D3D11_DEPTH_STENCIL_DESC dss = {};
    dss.DepthEnable = TRUE;
    dss.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dss.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dss.StencilEnable = FALSE;

    HR_T(m_pDevice->CreateDepthStencilState(&dss, &m_pDepthStencilState));
    m_pDeviceContext->OMSetDepthStencilState(m_pDepthStencilState, 0);

    // =========================================================================
    // 4) Bind default RT + viewport
    // =========================================================================
    m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = (float)m_ClientWidth;
    vp.Height = (float)m_ClientHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_pDeviceContext->RSSetViewports(1, &vp);

    return true;
}

void TutorialApp::UninitD3D()
{
    SAFE_RELEASE(m_pDepthStencilState);
    SAFE_RELEASE(m_pDepthStencilView);
    SAFE_RELEASE(m_pDepthStencil);
    SAFE_RELEASE(m_pRenderTargetView);
    SAFE_RELEASE(m_pDeviceContext);
    SAFE_RELEASE(m_pSwapChain);
    SAFE_RELEASE(m_pDevice);
}

// =========================================================================
// HDR Scene RT (R16G16B16A16_FLOAT)
// =========================================================================
bool TutorialApp::CreateSceneHDRResources(ID3D11Device* dev)
{
    mSceneHDRSRV.Reset();
    mSceneHDRRTV.Reset();
    mSceneHDRTex.Reset();

    // -------------------------------------------------------------------------
    // 0) Texture
    // -------------------------------------------------------------------------
    D3D11_TEXTURE2D_DESC td{};
    td.Width = m_ClientWidth;
    td.Height = m_ClientHeight;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HR_T(dev->CreateTexture2D(&td, nullptr, mSceneHDRTex.GetAddressOf()));

    // -------------------------------------------------------------------------
    // 1) RTV
    // -------------------------------------------------------------------------
    HR_T(dev->CreateRenderTargetView(mSceneHDRTex.Get(), nullptr, mSceneHDRRTV.GetAddressOf()));

    // -------------------------------------------------------------------------
    // 2) SRV
    // -------------------------------------------------------------------------
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = td.Format;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;

    HR_T(dev->CreateShaderResourceView(mSceneHDRTex.Get(), &sd, mSceneHDRSRV.GetAddressOf()));

    return true;
}

// =========================================================================
// GBuffer MRT (4 targets)
// =========================================================================
bool TutorialApp::CreateGBufferResources(ID3D11Device* dev)
{
    for (int i = 0; i < GBUF_COUNT; ++i)
    {
        mGBufferTex[i].Reset();
        mGBufferRTV[i].Reset();
        mGBufferSRV[i].Reset();
    }

    auto MakeRT = [&](int idx, DXGI_FORMAT fmt)
        {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = m_ClientWidth;
            td.Height = m_ClientHeight;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = fmt;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

            HR_T(dev->CreateTexture2D(&td, nullptr, mGBufferTex[idx].GetAddressOf()));
            HR_T(dev->CreateRenderTargetView(mGBufferTex[idx].Get(), nullptr, mGBufferRTV[idx].GetAddressOf()));

            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = fmt;
            sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MipLevels = 1;

            HR_T(dev->CreateShaderResourceView(mGBufferTex[idx].Get(), &sd, mGBufferSRV[idx].GetAddressOf()));
        };

    // -------------------------------------------------------------------------
    // Layout
    // 0: WorldPos (FP32)
    // 1: WorldNormal (FP16, raw -1..1 저장)
    // 2: BaseColor (UNORM)
    // 3: Metallic/Roughness (FP16x2)
    // -------------------------------------------------------------------------
    MakeRT(0, DXGI_FORMAT_R32G32B32A32_FLOAT);
    MakeRT(1, DXGI_FORMAT_R16G16B16A16_FLOAT);
    MakeRT(2, DXGI_FORMAT_R8G8B8A8_UNORM);
    MakeRT(3, DXGI_FORMAT_R16G16_FLOAT);

    return true;
}
