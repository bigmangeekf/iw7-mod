#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include "console/console.hpp"
#include "command.hpp"
#include "directx.hpp"
#include "frame_capture.hpp"
#include <utils/io.hpp>
#include <d3d11.h>
#include <wrl/client.h>

namespace frame_capture
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        std::atomic<bool> pending{false};
        std::mutex request_mutex;
        std::string requested_name;

        void capture(IDXGISwapChain* swap_chain, const std::string& name)
        {
            struct reset { ~reset() { pending = false; } } guard;
            try
            {
                if (!dx::device || !dx::deviceContext) throw std::runtime_error("D3D11 device unavailable");
                ComPtr<ID3D11Texture2D> source;
                if (!swap_chain || FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&source)))) throw std::runtime_error("swap chain back buffer unavailable");
                D3D11_TEXTURE2D_DESC desc{};
                source->GetDesc(&desc);
                const bool rgba = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                if (!rgba && !bgra) throw std::runtime_error("unsupported render target format " + std::to_string(desc.Format));
                if (!desc.Width || !desc.Height || desc.Width > 8192 || desc.Height > 8192 || desc.ArraySize != 1)
                    throw std::runtime_error("unsupported render target dimensions");
                auto copy_desc = desc;
                copy_desc.MipLevels = 1;
                copy_desc.SampleDesc = {1, 0};
                copy_desc.Usage = D3D11_USAGE_STAGING;
                copy_desc.BindFlags = 0;
                copy_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                copy_desc.MiscFlags = 0;
                ComPtr<ID3D11Texture2D> staging;
                if (FAILED(dx::device->CreateTexture2D(&copy_desc, nullptr, &staging))) throw std::runtime_error("staging allocation failed");
                if (desc.SampleDesc.Count > 1)
                {
                    auto resolve_desc = copy_desc;
                    resolve_desc.Usage = D3D11_USAGE_DEFAULT;
                    resolve_desc.CPUAccessFlags = 0;
                    ComPtr<ID3D11Texture2D> resolved;
                    if (FAILED(dx::device->CreateTexture2D(&resolve_desc, nullptr, &resolved))) throw std::runtime_error("resolve allocation failed");
                    dx::deviceContext->ResolveSubresource(resolved.Get(), 0, source.Get(), 0, desc.Format);
                    dx::deviceContext->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, resolved.Get(), 0, nullptr);
                }
                else dx::deviceContext->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source.Get(), 0, nullptr);

                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (FAILED(dx::deviceContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) throw std::runtime_error("staging map failed");
                struct unmap { ID3D11DeviceContext* context; ID3D11Texture2D* texture; ~unmap() { context->Unmap(texture, 0); } } mapping{dx::deviceContext, staging.Get()};
                BITMAPFILEHEADER file{};
                BITMAPINFOHEADER info{};
                file.bfType = 0x4D42;
                file.bfOffBits = sizeof(file) + sizeof(info);
                file.bfSize = file.bfOffBits + desc.Width * desc.Height * 4;
                info.biSize = sizeof(info);
                info.biWidth = static_cast<LONG>(desc.Width);
                info.biHeight = -static_cast<LONG>(desc.Height);
                info.biPlanes = 1;
                info.biBitCount = 32;
                info.biCompression = BI_RGB;
                std::string bytes(file.bfSize, '\0');
                memcpy(bytes.data(), &file, sizeof(file));
                memcpy(bytes.data() + sizeof(file), &info, sizeof(info));
                for (UINT y = 0; y < desc.Height; ++y)
                {
                    const auto* row = static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch;
                    auto* output = reinterpret_cast<unsigned char*>(bytes.data() + file.bfOffBits + y * desc.Width * 4);
                    for (UINT x = 0; x < desc.Width; ++x)
                    {
                        output[x * 4] = row[x * 4 + (rgba ? 2 : 0)];
                        output[x * 4 + 1] = row[x * 4 + 1];
                        output[x * 4 + 2] = row[x * 4 + (rgba ? 0 : 2)];
                        output[x * 4 + 3] = 255;
                    }
                }
                const auto path = "iw7-mod/screenshots/" + name + ".bmp";
                if (!utils::io::write_file(path, bytes)) throw std::runtime_error("BMP write failed");
                console::info("[capture_frame] Saved %s (%ux%u, format=%u).\n", path.c_str(), desc.Width, desc.Height, desc.Format);
            }
            catch (const std::exception& e) { console::error("[capture_frame] %s\n", e.what()); }
        }
    }

    // Called under the existing render mutex immediately before Present.
    void on_present(IDXGISwapChain* swap_chain)
    {
        std::string name;
        {
            std::lock_guard lock(request_mutex);
            name.swap(requested_name);
        }
        if (!name.empty()) capture(swap_chain, name);
    }

    class component final : public component_interface
    {
    public:
        void post_unpack() override
        {
            if (game::environment::is_dedi()) return;
            command::add("capture_frame", [](const command::params& args)
            {
                const std::string name = args.size() == 1 ? "frame" : args.get(1);
                if (args.size() > 2 || name.empty() || name.size() > 64 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
                {
                    console::error("Usage: capture_frame [filename containing letters, digits, _ or -]\n");
                    return;
                }
                if (pending.exchange(true)) { console::error("[capture_frame] Capture already pending.\n"); return; }
                std::lock_guard lock(request_mutex);
                requested_name = name;
            });
        }
    };
}
REGISTER_COMPONENT(frame_capture::component)
