#pragma once

// Forward declarations
struct AmdExtFfxApi;
struct AmdExtAntiLagApi;
struct AmdExtD3DFactory;
struct AmdExtD3DShaderIntrinsics;
struct AmdExtD3DDevice8;

typedef HRESULT(__cdecl* PFN_AmdExtD3DCreateInterface)(IUnknown* pOuter, REFIID riid, void** ppvObject);

// Based on the PDBs provided with FFX 2.1.0
enum AmdExtWaveMatrixPropertiesType : int32_t
{
    float16 = 0x0,
    float32 = 0x1,
    float64 = 0x2,
    sint8 = 0x3,
    sint16 = 0x4,
    sint32 = 0x5,
    sint64 = 0x6,
    uint8 = 0x7,
    uint16 = 0x8,
    uint32 = 0x9,
    uint64 = 0xA,
    fp8 = 0xB,
    bf8 = 0xC,
};

struct __declspec(align(8)) AmdExtWaveMatrixProperties
{
    unsigned __int64 mSize;
    unsigned __int64 nSize;
    unsigned __int64 kSize;
    AmdExtWaveMatrixPropertiesType aType;
    AmdExtWaveMatrixPropertiesType bType;
    AmdExtWaveMatrixPropertiesType cType;
    AmdExtWaveMatrixPropertiesType resultType;
    bool saturatingAccumulation;
};

MIDL_INTERFACE("F714E11A-B54E-4E0F-ABC5-DF58B18133D1")
IAmdExtD3DDevice8 : public IUnknown
{
    virtual HRESULT unknown1() = 0;  // not used
    virtual HRESULT unknown2() = 0;  // not used
    virtual HRESULT unknown3() = 0;  // not used
    virtual HRESULT unknown4() = 0;  // not used
    virtual HRESULT unknown5() = 0;  // not used
    virtual HRESULT unknown6() = 0;  // not used
    virtual HRESULT unknown7() = 0;  // not used
    virtual HRESULT unknown8() = 0;  // not used
    virtual HRESULT unknown9() = 0;  // not used
    virtual HRESULT unknown10() = 0; // not used
    virtual HRESULT unknown11() = 0; // not used
    virtual HRESULT unknown12() = 0; // not used
    virtual HRESULT unknown13() = 0; // not used
    virtual HRESULT GetWaveMatrixProperties(uint64_t* a, AmdExtWaveMatrixProperties* waveMatrixProperties) = 0;
};

enum AmdExtD3DShaderIntrinsicsSupport : int32_t
{
    AmdExtD3DShaderIntrinsicsSupport_Readfirstlane = 0x1,
    AmdExtD3DShaderIntrinsicsSupport_Readlane = 0x2,
    AmdExtD3DShaderIntrinsicsSupport_LaneId = 0x3,
    AmdExtD3DShaderIntrinsicsSupport_Swizzle = 0x4,
    AmdExtD3DShaderIntrinsicsSupport_Ballot = 0x5,
    AmdExtD3DShaderIntrinsicsSupport_MBCnt = 0x6,
    AmdExtD3DShaderIntrinsicsSupport_Compare3 = 0x7,
    AmdExtD3DShaderIntrinsicsSupport_Barycentrics = 0x8,
    AmdExtD3DShaderIntrinsicsSupport_WaveReduce = 0x9,
    AmdExtD3DShaderIntrinsicsSupport_WaveScan = 0xA,
    AmdExtD3DShaderIntrinsicsSupport_LoadDwordAtAddr = 0xB,
    AmdExtD3DShaderIntrinsicsSupport_Reserved1 = 0xC,
    AmdExtD3DShaderIntrinsicsSupport_IntersectInternal = 0xD,
    AmdExtD3DShaderIntrinsicsSupport_DrawIndex = 0xE,
    AmdExtD3DShaderIntrinsicsSupport_AtomicU64 = 0xF,
    AmdExtD3DShaderIntrinsicsSupport_BaseInstance = 0x10,
    AmdExtD3DShaderIntrinsicsSupport_BaseVertex = 0x11,
    AmdExtD3DShaderIntrinsicsSupport_FloatConversion = 0x12,
    AmdExtD3DShaderIntrinsicsSupport_GetWaveSize = 0x13,
    AmdExtD3DShaderIntrinsicsSupport_ReadlaneAt = 0x14,
    AmdExtD3DShaderIntrinsicsSupport_RayTraceHitToken = 0x15,
    AmdExtD3DShaderIntrinsicsSupport_ShaderClock = 0x16,
    AmdExtD3DShaderIntrinsicsSupport_ShaderRealtimeClock = 0x17,
    AmdExtD3DShaderIntrinsicsSupport_Halt = 0x18,
    AmdExtD3DShaderIntrinsicsSupport_IntersectBvhNode = 0x19,
    AmdExtD3DShaderIntrinsicsSupport_BufferStoreByte = 0x1A,
    AmdExtD3DShaderIntrinsicsSupport_BufferStoreShort = 0x1B,
    AmdExtD3DShaderIntrinsicsSupport_ShaderMarker = 0x1C,
    AmdExtD3DShaderIntrinsicsSupport_FloatOpWithRoundMode = 0x1D,
    AmdExtD3DShaderIntrinsicsSupport_Reserved2 = 0x1E,
    AmdExtD3DShaderIntrinsicsSupport_WaveMatrix = 0x1F,
    AmdExtD3DShaderIntrinsicsSupport_Float8Conversion = 0x20,
    AmdExtD3DShaderIntrinsicsSupport_Builtins = 0x21,
    AmdExtD3DShaderIntrinsicsSupport_LoadByteAtAddr = 0x22,
};

MIDL_INTERFACE("BA019D53-CCAB-4CBD-B56A-7230ED4330AD")
IAmdExtD3DShaderIntrinsics : public IUnknown
{
  public:
    virtual HRESULT GetInfo(void* ShaderIntrinsicsInfo) = 0; // not used
    virtual HRESULT CheckSupport(AmdExtD3DShaderIntrinsicsSupport intrinsic) = 0;
    virtual HRESULT Enable() = 0;
};

MIDL_INTERFACE("014937EC-9288-446F-A9AC-D75A8E3A984F")
IAmdExtD3DFactory : public IUnknown
{
  public:
    virtual HRESULT CreateInterface(IUnknown * pOuter, REFIID riid, void** ppvObject) = 0;
};

MIDL_INTERFACE("b58d6601-7401-4234-8180-6febfc0e484c")
IAmdExtFfxApi : public IUnknown
{
  public:
    virtual HRESULT UpdateFfxApiProvider(void* pData, uint32_t dataSizeInBytes) = 0;
};

struct AmdExtFlipMeterCaps
{
    uint32_t supportedModesMask; // bit (mode - 1)
    uint32_t supported;
    uint32_t unknown;
    uint32_t status;
};

struct AmdExtFlipMeterConfig
{
    uint32_t mode; // 0 = off, FSR requests 2
    uint32_t reserved;
    uint32_t valid;
    uint32_t flags;
    uint32_t unknown10;
    uint32_t status;
    uint8_t unknown18[0x28];
};
static_assert(sizeof(AmdExtFlipMeterConfig) == 0x40);

MIDL_INTERFACE("543820DD-06C9-4F7E-A3D0-CA260D794ADD")
IAmdExtFlipMeter : public IUnknown
{
  public:
    virtual HRESULT GetCaps(uint32_t index, AmdExtFlipMeterCaps* caps, uint32_t capsSize) = 0;
    virtual HRESULT SetConfig(AmdExtFlipMeterConfig * config) = 0;
    virtual HRESULT GetConfig(AmdExtFlipMeterConfig * config) = 0;
};

typedef HRESULT(STDMETHODCALLTYPE* PFN_AmdExtD3DFactory_CreateInterface)(IAmdExtD3DFactory* This, IUnknown* pOuter,
                                                                         REFIID riid, void** ppvObject);
typedef HRESULT(STDMETHODCALLTYPE* PFN_AmdExtFlipMeter_Config)(IAmdExtFlipMeter* This, AmdExtFlipMeterConfig* config);
typedef ULONG(STDMETHODCALLTYPE* PFN_AmdExtFlipMeter_Release)(IAmdExtFlipMeter* This);

class Amdxc64Hooks
{
  public:
    inline static HMODULE moduleAmdxc64 = nullptr;

    inline static AmdExtD3DDevice8* amdExtD3DDevice8 = nullptr;
    inline static AmdExtD3DShaderIntrinsics* amdExtD3DShaderIntrinsics = nullptr;
    inline static AmdExtD3DShaderIntrinsics* o_amdExtD3DShaderIntrinsics = nullptr;
    inline static AmdExtFfxApi* amdExtFfxApi = nullptr;
    inline static AmdExtAntiLagApi* amdExtAntiLagApi = nullptr;

    inline static PFN_AmdExtD3DCreateInterface o_AmdExtD3DCreateInterface = nullptr;
    inline static PFN_AmdExtD3DFactory_CreateInterface o_FactoryCreateInterface = nullptr;
    inline static PFN_AmdExtFlipMeter_Config o_FlipMeterSetConfig = nullptr;
    inline static PFN_AmdExtFlipMeter_Config o_FlipMeterGetConfig = nullptr;
    inline static PFN_AmdExtFlipMeter_Release o_FlipMeterRelease = nullptr;

    inline static bool giveGameAl2Proxy = true;

    static void Init();
    static HRESULT hkAmdExtD3DCreateInterface(IUnknown* pOuter, REFIID riid, void** ppvObject);

    // Re-sends the last config FSR requested, with DisableFlipMetering applied
    static void ApplyFlipMeterOverride();

  private:
    // Recursive in case the driver releases the interface from within its own calls
    inline static std::recursive_mutex flipMeterMutex;
    // Not ref counted to not keep the swapchain alive, cleared on its final Release
    inline static IAmdExtFlipMeter* flipMeter = nullptr;
    inline static std::optional<AmdExtFlipMeterConfig> flipMeterRequested {};

    static HRESULT SetFlipMeterConfig(IAmdExtFlipMeter* flipMeter, const AmdExtFlipMeterConfig& requested);
    static ULONG STDMETHODCALLTYPE hkFlipMeterRelease(IAmdExtFlipMeter* This);
    static void HookFactory(IAmdExtD3DFactory* factory);
    static void HookFlipMeter(IAmdExtFlipMeter* flipMeter);
    static HRESULT STDMETHODCALLTYPE hkFactoryCreateInterface(IAmdExtD3DFactory* This, IUnknown* pOuter, REFIID riid,
                                                              void** ppvObject);
    static HRESULT STDMETHODCALLTYPE hkFlipMeterSetConfig(IAmdExtFlipMeter* This, AmdExtFlipMeterConfig* config);
    static HRESULT STDMETHODCALLTYPE hkFlipMeterGetConfig(IAmdExtFlipMeter* This, AmdExtFlipMeterConfig* config);
};
