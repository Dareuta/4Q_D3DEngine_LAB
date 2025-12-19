#pragma once

#include <d3d11.h> // 이거 없으면 불완전 타입? 이라고 함, 위험할 수 있어서 고정
#include <memory>

struct ID3D11ShaderResourceView;

class Texture2DResource
{
public:
	Texture2DResource(ID3D11ShaderResourceView* srv,
		unsigned int width, unsigned int height) : m_srv(srv), m_width(width), m_height(height) {
		// 아무것도 안함
	}

	~Texture2DResource() {
		if (m_srv)
		{
			m_srv->Release();
			m_srv = nullptr;
		}
	}

	// 복사 이동 금지
	Texture2DResource(const Texture2DResource&) = delete;
	Texture2DResource& operator=(const Texture2DResource&) = delete;
	Texture2DResource(Texture2DResource&&) = delete;
	Texture2DResource& operator=(Texture2DResource&&) = delete;

	ID3D11ShaderResourceView* GetSRV()   const { return m_srv; }
	unsigned int              GetWidth() const { return m_width; }
	unsigned int              GetHeight() const { return m_height; }

private:
	ID3D11ShaderResourceView* m_srv = nullptr;
	unsigned int m_width = 0;
	unsigned int m_height = 0;
};