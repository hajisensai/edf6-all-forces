// worldmap_text.h: GDI labels and the pins' sprites as D3D11 textures.
#include "worldmap_text.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace crew::worldmaptext {
namespace {
using Microsoft::WRL::ComPtr;

const wchar_t* FaceFor(hudtext::Lang lang) noexcept {
    switch(lang) {
    case hudtext::Lang::zhCN: return L"Microsoft YaHei UI";
    case hudtext::Lang::zhTW: return L"Microsoft JhengHei UI";
    case hudtext::Lang::ja: return L"Yu Gothic UI";
    default: return L"Segoe UI";
    }
}

// White BGRA pixels with the alpha given, as an immutable texture's view.
ComPtr<ID3D11ShaderResourceView> Texture(ID3D11Device* device,const std::vector<unsigned>& bgra,int w,int h) noexcept {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=static_cast<UINT>(w);
    desc.Height=static_cast<UINT>(h);
    desc.MipLevels=1;
    desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_IMMUTABLE;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{bgra.data(),static_cast<UINT>(w)*4u,0};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    if(FAILED(device->CreateTexture2D(&desc,&data,&texture)) ||
       FAILED(device->CreateShaderResourceView(texture.Get(),nullptr,&view)))return nullptr;
    return view;
}

// A 64 x 64 sprite: alpha = coverage of `inside(distance from the centre in texels)` sampled 4 x 4 a texel.
template<class Inside> ComPtr<ID3D11ShaderResourceView> Sprite(ID3D11Device* device,Inside inside) noexcept {
    constexpr int n=64;
    std::vector<unsigned> px(static_cast<std::size_t>(n*n));
    for(int y=0;y<n;++y)
        for(int x=0;x<n;++x) {
            int hits=0;
            for(int sy=0;sy<4;++sy)
                for(int sx=0;sx<4;++sx) {
                    const float dx=static_cast<float>(x)+(static_cast<float>(sx)+0.5f)/4.f-n*0.5f;
                    const float dy=static_cast<float>(y)+(static_cast<float>(sy)+0.5f)/4.f-n*0.5f;
                    hits+=inside(std::sqrt(dx*dx+dy*dy)) ? 1 : 0;
                }
            const unsigned a=static_cast<unsigned>(hits*255/16);
            px[static_cast<std::size_t>(y*n+x)]=(a<<24)|0x00FFFFFFu;
        }
    return Texture(device,px,n,n);
}

}  // namespace

struct Labels::Impl {
    struct Entry {
        std::wstring text;
        float size;
        bool bold;
        int lang;
        float scale;
        unsigned used;
        ComPtr<ID3D11ShaderResourceView> view;
        float w,h;
    };
    ID3D11Device* device=nullptr;   // the device the textures are of (a new one: all dropped)
    std::vector<Entry> entries;
    ComPtr<ID3D11ShaderResourceView> disc,ring;

    void Bind(ID3D11Device* d) noexcept {
        if(d==device)return;
        entries.clear();
        disc.Reset();
        ring.Reset();
        device=d;
    }

    // `text` rasterized `px` target pixels tall: its view and size in pixels; false when GDI or D3D fails.
    bool Make(const wchar_t* text,int px,bool bold,hudtext::Lang lang,Entry& e) noexcept {
        const int len=static_cast<int>(std::wcslen(text));
        if(len<=0)return false;
        HDC dc=CreateCompatibleDC(nullptr);
        if(!dc)return false;
        HFONT font=CreateFontW(-px,0,0,0,bold ? FW_BOLD : FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_TT_PRECIS,
                               CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_DONTCARE,FaceFor(lang));
        bool ok=false;
        if(font) {
            HGDIOBJ was=SelectObject(dc,font);
            SIZE size{};
            if(GetTextExtentPoint32W(dc,text,len,&size) && size.cx>0 && size.cy>0) {
                const int pad=2;
                const int w=size.cx+2*pad>4096 ? 4096 : size.cx+2*pad,h=size.cy+2*pad;
                BITMAPINFO bi{};
                bi.bmiHeader.biSize=sizeof(bi.bmiHeader);
                bi.bmiHeader.biWidth=w;
                bi.bmiHeader.biHeight=-h;   // top down
                bi.bmiHeader.biPlanes=1;
                bi.bmiHeader.biBitCount=32;
                bi.bmiHeader.biCompression=BI_RGB;
                void* bits=nullptr;
                HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
                if(bitmap && bits) {
                    HGDIOBJ wasBitmap=SelectObject(dc,bitmap);
                    std::memset(bits,0,static_cast<std::size_t>(w)*static_cast<std::size_t>(h)*4u);
                    SetBkMode(dc,TRANSPARENT);
                    SetTextColor(dc,RGB(255,255,255));
                    TextOutW(dc,pad,pad,text,len);
                    GdiFlush();
                    const unsigned* src=static_cast<const unsigned*>(bits);
                    std::vector<unsigned> pixels(static_cast<std::size_t>(w)*static_cast<std::size_t>(h));
                    for(std::size_t i=0;i<pixels.size();++i) {
                        const unsigned c=src[i];
                        unsigned a=c&0xFFu;
                        if(((c>>8)&0xFFu)>a)a=(c>>8)&0xFFu;
                        if(((c>>16)&0xFFu)>a)a=(c>>16)&0xFFu;
                        pixels[i]=(a<<24)|0x00FFFFFFu;
                    }
                    e.view=Texture(device,pixels,w,h);
                    e.w=static_cast<float>(w);
                    e.h=static_cast<float>(h);
                    ok=e.view!=nullptr;
                    SelectObject(dc,wasBitmap);
                }
                if(bitmap)DeleteObject(bitmap);
            }
            SelectObject(dc,was);
            DeleteObject(font);
        }
        DeleteDC(dc);
        return ok;
    }
};

Labels::Labels() noexcept : impl_(new(std::nothrow) Impl()) {}
Labels::~Labels() noexcept=default;

Label Labels::Get(worldmapgpu::Painter& paint,const wchar_t* text,float size,bool bold,hudtext::Lang lang,unsigned frame) noexcept {
    ID3D11Device* const device=paint.Device();
    if(!impl_ || !device || !text || !*text)return Label{};
    Impl& s=*impl_;
    s.Bind(device);
    const float scale=paint.Scale();
    for(Impl::Entry& e:s.entries)
        if(e.size==size && e.bold==bold && e.lang==static_cast<int>(lang) && e.scale==scale && e.text==text) {
            e.used=frame;
            return Label{e.view.Get(),e.w/scale,e.h/scale};
        }
    Impl::Entry e{};
    try { e.text=text; } catch(...) { return Label{}; }
    e.size=size;e.bold=bold;e.lang=static_cast<int>(lang);e.scale=scale;e.used=frame;
    const int px=static_cast<int>(std::lround(size*scale));
    if(!s.Make(text,px<6 ? 6 : px,bold,lang,e))return Label{};
    const Label made{e.view.Get(),e.w/scale,e.h/scale};
    if(s.entries.size()>=static_cast<std::size_t>(kLabels)) {
        // The least recently drawn (never one of this frame: its view is in use).
        std::size_t victim=s.entries.size();
        for(std::size_t i=0;i<s.entries.size();++i)
            if(s.entries[i].used!=frame && (victim==s.entries.size() || s.entries[i].used<s.entries[victim].used))victim=i;
        if(victim==s.entries.size())return Label{};   // full of this frame's labels: this one next frame
        s.entries[victim]=std::move(e);
        return made;
    }
    try { s.entries.push_back(std::move(e)); } catch(...) { return Label{}; }
    return made;
}

ID3D11ShaderResourceView* Labels::Disc(worldmapgpu::Painter& paint) noexcept {
    ID3D11Device* const device=paint.Device();
    if(!impl_ || !device)return nullptr;
    impl_->Bind(device);
    if(!impl_->disc)impl_->disc=Sprite(device,[](float d) { return d<=31.f; });
    return impl_->disc.Get();
}
ID3D11ShaderResourceView* Labels::Ring(worldmapgpu::Painter& paint) noexcept {
    ID3D11Device* const device=paint.Device();
    if(!impl_ || !device)return nullptr;
    impl_->Bind(device);
    if(!impl_->ring)impl_->ring=Sprite(device,[](float d) { return d<=31.f && d>=25.f; });
    return impl_->ring.Get();
}

void Labels::Reset() noexcept {
    if(!impl_)return;
    impl_->entries.clear();
    impl_->disc.Reset();
    impl_->ring.Reset();
    impl_->device=nullptr;
}

}  // namespace crew::worldmaptext
