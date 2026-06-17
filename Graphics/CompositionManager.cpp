#include "CompositionManager.h"

// ─── DWM Acrylic Blur ─────────────────────────────────────────────────────────

enum class AccentState : DWORD
{
    Disabled                = 0,
    EnableBlurBehind        = 3,
    EnableAcrylicBlurBehind = 4,
};

struct AccentPolicy
{
    AccentState AccentState;
    DWORD       AccentFlags;
    DWORD       GradientColor; // AABBGGRR
    DWORD       AnimationId;
};

struct WcaData
{
    DWORD  Attribute; // 19 = WCA_ACCENT_POLICY
    PVOID  pvData;
    SIZE_T cbData;
};

static void ApplyAcrylicBlur(HWND hwnd, DWORD gradientColor)
{
    using Fn = BOOL(WINAPI*)(HWND, WcaData*);
    static Fn fn = reinterpret_cast<Fn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute")
    );
    if (!fn) return;

    AccentPolicy policy{};
    policy.AccentState   = AccentState::EnableAcrylicBlurBehind;
    policy.AccentFlags   = 0;
    policy.GradientColor = gradientColor;
    policy.AnimationId   = 0;

    WcaData data{};
    data.Attribute = 19;
    data.pvData    = &policy;
    data.cbData    = sizeof(policy);

    fn(hwnd, &data);
}

// ─── Interface publique ───────────────────────────────────────────────────────

void CompositionManager::Initialize(HWND hwnd)
{
    // 0x40000000 = noir 25% opaque → fond sombre dépoli style Dynamic Island
    ApplyAcrylicBlur(hwnd, 0x40000000);
}

void CompositionManager::Resize(int /*width*/, int /*height*/)
{
    // DWM gère le redimensionnement automatiquement, rien à faire ici
}

void CompositionManager::SetScale(float /*scale*/)
{
    // Les animations de scale sont gérées par WindowManager via SetWindowPos
}
