#pragma once
struct IDXGISwapChain;
namespace frame_capture
{
    void on_present(IDXGISwapChain* swap_chain);
}
