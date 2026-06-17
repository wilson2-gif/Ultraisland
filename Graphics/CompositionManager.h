#pragma once
#include <Windows.h>

class CompositionManager
{
public:
    CompositionManager()  = default;
    ~CompositionManager() = default;

    void Initialize(HWND hwnd);
    void Resize(int width, int height);
    void SetScale(float scale);
};
