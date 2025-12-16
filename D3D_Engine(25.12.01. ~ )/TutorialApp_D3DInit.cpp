// InitD3D / UninitD3D

#include "TutorialApp.h"
#include "../D3D_Core/pch.h"

bool TutorialApp::InitD3D()
{
	HRESULT hr = 0;

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
		NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
		creationFlags, NULL, NULL, D3D11_SDK_VERSION,
		&swapDesc,
		&m_pSwapChain,
		&m_pDevice, NULL,
		&m_pDeviceContext));

	ID3D11Texture2D* pBackBufferTexture = nullptr;

	HR_T(m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBufferTexture));

	HR_T(m_pDevice->CreateRenderTargetView(pBackBufferTexture, NULL, &m_pRenderTargetView));
	SAFE_RELEASE(pBackBufferTexture);

	//==============================================================================================

	D3D11_TEXTURE2D_DESC dsDesc = {};
	dsDesc.Width = m_ClientWidth;
	dsDesc.Height = m_ClientHeight;
	dsDesc.MipLevels = 1;
	dsDesc.ArraySize = 1;
	dsDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // 흔한 조합
	dsDesc.SampleDesc.Count = 1;  // 스왑체인과 동일하게
	dsDesc.SampleDesc.Quality = 0;
	dsDesc.Usage = D3D11_USAGE_DEFAULT;
	dsDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	HR_T(m_pDevice->CreateTexture2D(&dsDesc, nullptr, &m_pDepthStencil));
	HR_T(m_pDevice->CreateDepthStencilView(m_pDepthStencil, nullptr, &m_pDepthStencilView));

	D3D11_DEPTH_STENCIL_DESC dss = {};
	dss.DepthEnable = TRUE;
	dss.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	dss.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; // 스카이박스 쓸거면 LEQUAL이 편함. 기본은 LESS
	dss.StencilEnable = FALSE;
	HR_T(m_pDevice->CreateDepthStencilState(&dss, &m_pDepthStencilState));
	m_pDeviceContext->OMSetDepthStencilState(m_pDepthStencilState, 0);

	//==============================================================================================

	m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = (float)m_ClientWidth;
	viewport.Height = (float)m_ClientHeight;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	m_pDeviceContext->RSSetViewports(1, &viewport);

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