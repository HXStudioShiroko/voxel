#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <d2d1.h>
#include <dwrite.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <wincodec.h>

#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <random>
#include <memory>
#include <fstream> // 如果没有的话加上
#include "json.hpp" // 引入 JSON 解析
#include <queue>   // ★ 加上这个，解决 C2039: "queue": 不是 "std" 的成员
#include <array>   // ★ 加上这个，解决 std::array 的类型错误
#include <condition_variable>
#include <deque>
#include <imm.h>
#include <cstddef>






#pragma comment(lib, "imm32.lib")



#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using namespace DirectX;
using namespace std;
using Microsoft::WRL::ComPtr;
using json = nlohmann::json;


static ComPtr<ID3D11PixelShader> g_skyPS;
static ComPtr<ID3D11PixelShader> g_cloudPS;         // ★ BV 积云
static ComPtr<ID3D11ShaderResourceView> g_depthSRV; // ★ 云 pass 读深度用












// ============================================================
// Section 1. 全局常量
// ============================================================
struct ModSettings {
    bool  loaded = false;
    float skyColor[3] = {0.55f, 0.65f, 0.80f};
    bool  fogEnable = true;
    float fogDensity = 0.01f;
    float fogColor[3] = {0.55f, 0.65f, 0.80f};
    float noiseAmplitude = 1.0f;
    bool  flatMode = false;
    // ★ 新增属性
    bool  noHeightLimit = false;
    bool  gridMode = false;
} g_modSettings; // <--- 千万别漏了这里的 ; 分号
static const int CHUNK_SIZE = 16;
static const int CHUNK_AREA = CHUNK_SIZE * CHUNK_SIZE;
static const int RH = 1024;
static const int RY0 = -60;
static const int RY1 = RY0 + RH;
static const int SEA_LEVEL = 63;

static const size_t CHUNK_BLOCK_COUNT = (size_t)CHUNK_AREA * RH;
static const size_t CHUNK_BIOME_COUNT = (size_t)CHUNK_AREA;

// 原:
// static const int LOAD_RADIUS = 16;
// static const int UNLOAD_RADIUS = 20;
// static const float RENDER_DISTANCE = 320.0f;

static const int FULL_RADIUS = 5;  // ★ 实加载(精修)半径(chunk):11×11=121,桩在其外
static const int STUB_MESH_RADIUS = 9;  // ★ 桩只在这个半径内建网格(更远的桩只为 GI 服务)
static int g_loadRadius = 10;      // 外圈流式半径(21×21=441 chunk ≈ 232MB,MC 默认视距量级)
static int g_unloadRadius = 13;    // = g_loadRadius + 3

static float g_renderDistance = 160.0f;   // = g_loadRadius * 16 格
static const int MAX_LOADED_CHUNKS = 1024; // ★ 硬上限:超过就卸载最远的,内存封顶
static const int ROOM_MAX = 15;   // (2*15+1)^2=961 < MAX_LOADED_CHUNKS=1024,避免满载抖动


static const int LOAD_BUDGET_PER_FRAME = 2;
static const int STARTUP_FAST_LOAD_UNTIL = 100;
static const int LOAD_BUDGET_FAST = 4;

static const int MAX_MESH_REBUILD_PER_FRAME = 8;

//static const float RENDER_DISTANCE = 320.0f;

// ★ 粒子系统
static const int   MAX_PARTICLES       = 4096;
static const float PARTICLE_GRAVITY    = 18.0f;
static const int   PARTICLES_PER_BREAK = 24;

static const float PLAYER_WIDTH      = 0.6f;
static const float PLAYER_HEIGHT     = 1.8f;
static const float PLAYER_HALF_W     = PLAYER_WIDTH * 0.5f;
static const float PLAYER_EYE_HEIGHT = 1.62f;
static const float GRAVITY           = 10.0f;

static const float WALK_SPEED_NORMAL = 4.0f;
static const float WALK_SPEED_SPRINT = 7.0f;

static const float JUMP_VELOCITY     = 5.477f;
static const float SWIM_UP_SPEED     = 3.0f;
static const float SWIM_DOWN_SPEED   = -2.0f;
static const float FLY_SPEED         = 60.0f;
static const float TERMINAL_VELOCITY = -80.0f;

static int  g_width  = 1280;
static int  g_height = 720;

// ★ 嵌入资源 ID
#define IDR_STONE         101
#define IDR_DEEPSLATE     102
#define IDR_DIRT          103
#define IDR_SAND          104
#define IDR_GRASS_TOP     105
#define IDR_LOG_TOP       106
#define IDR_LOG_SIDE      107
#define IDR_TUFF          108
#define IDR_CALCITE       109
#define IDR_ANDESITE      110
#define IDR_DIORITE       111
#define IDR_GRANITE       112
#define IDR_PODZOL_TOP    113
#define IDR_PODZOL_SIDE   114
#define IDR_MYCELIUM      115
#define IDR_MUD           116
#define IDR_RED_SAND      117
#define IDR_GRAVEL        118
#define IDR_CLAY          119
#define IDR_SANDSTONE     120
#define IDR_RED_SANDSTONE 121
#define IDR_TERRACOTTA    122
#define IDR_PLANKS        123
#define IDR_COAL_ORE      124
#define IDR_IRON_ORE      125
#define IDR_COPPER_ORE    126
#define IDR_GOLD_ORE      127
#define IDR_REDSTONE_ORE  128
#define IDR_LAPIS_ORE     129
#define IDR_DIAMOND_ORE   130
#define IDR_EMERALD_ORE   131

enum TexLayer : uint8_t {
    TEX_STONE         = 0,
    TEX_DEEPSLATE     = 1,
    TEX_DIRT          = 2,
    TEX_SAND          = 3,
    TEX_GRASS_TOP     = 4,
    TEX_LOG_TOP       = 5,
    TEX_LOG_SIDE      = 6,
    TEX_TUFF          = 7,
    TEX_CALCITE       = 8,
    TEX_ANDESITE      = 9,
    TEX_DIORITE       = 10,
    TEX_GRANITE       = 11,
    TEX_PODZOL_TOP    = 12,
    TEX_PODZOL_SIDE   = 13,
    TEX_MYCELIUM      = 14,
    TEX_MUD           = 15,
    TEX_RED_SAND      = 16,
    TEX_GRAVEL        = 17,
    TEX_CLAY          = 18,
    TEX_SANDSTONE     = 19,
    TEX_RED_SANDSTONE = 20,
    TEX_TERRACOTTA    = 21,
    TEX_PLANKS        = 22,
    TEX_COAL_ORE      = 23,
    TEX_IRON_ORE      = 24,
    TEX_COPPER_ORE    = 25,
    TEX_GOLD_ORE      = 26,
    TEX_REDSTONE_ORE  = 27,
    TEX_LAPIS_ORE     = 28,
    TEX_DIAMOND_ORE   = 29,
    TEX_EMERALD_ORE   = 30,
    TEX_COUNT         = 31,
    TEX_NONE          = 255
};

// ============================================================
// Section 2. 方块
// ============================================================
enum class Block : uint8_t {
    AIR = 0,
    STONE, DEEPSLATE, TUFF, CALCITE,
    ANDESITE, DIORITE, GRANITE,
    DIRT, GRASS, PODZOL, MYCELIUM, MUD,
    SAND, RED_SAND, GRAVEL, CLAY,
    SANDSTONE, RED_SANDSTONE, TERRACOTTA,
    SNOW_BLOCK, ICE, PACKED_ICE, BLUE_ICE,
    WATER, LAVA,
    COAL_ORE, IRON_ORE, COPPER_ORE, GOLD_ORE,
    REDSTONE_ORE, LAPIS_ORE, DIAMOND_ORE, EMERALD_ORE,
    OAK_LOG, OAK_LEAVES, SPRUCE_LOG, SPRUCE_LEAVES,
    BIRCH_LOG, BIRCH_LEAVES, JUNGLE_LOG, JUNGLE_LEAVES,
    ACACIA_LOG, ACACIA_LEAVES, DARK_OAK_LOG, DARK_OAK_LEAVES,
    CACTUS, DEAD_BUSH, SHORT_GRASS, TALL_GRASS, FERN,
    MOSS_CARPET, MUSHROOM_RED, MUSHROOM_BROWN,
    SUGAR_CANE, BAMBOO, VINE,
    GLASS, BRICK, PLANKS,
    MISSING,
    COUNT
};

static const int BLOCK_COUNT = (int)Block::COUNT;

struct BlockInfo {
    uint8_t r, g, b;
    bool  transparent;
    bool  liquid;
    bool  solid;
    uint8_t tex[6];
};
static BlockInfo g_blockInfo[BLOCK_COUNT];


// =====================================================================
//  CPU ReSTIR GI —— 单文件整合版
//  算法: "A Gentle Introduction to ReSTIR" (Wyman et al., SIGGRAPH 2023 公开课程)
//  结构参考 Alpha Piscium (GPL-3.0, (c) Luna5ama) 的 C++ 重实现;本项目按 GPL-3.0 开源
// =====================================================================
namespace cigi {

    // ---------------- 用户接线区(只有这 3 处要接你的代码) ----------------
    // 1) 方块查询:返回方块ID,0=空气。改成你现有的 chunk 查询函数
    // 1) 方块查询:函数指针,由 giInit 指向 cigi_support::blockAtFast
    static int (*s_query)(int, int, int) = nullptr;
    static inline int blockAt(int x, int y, int z) {
        return s_query ? s_query(x, y, z) : 0;
    }

    // 2) 方块颜色:直接读你已有的 g_blockInfo 纯色字段
    static inline void blockColor(int id, float c[3]) {
        c[0] = g_blockInfo[id].r / 255.0f;
        c[1] = g_blockInfo[id].g / 255.0f;
        c[2] = g_blockInfo[id].b / 255.0f;
    }
    // 3) 相机:每帧由 giSetCamera 传入(用你现有的 view*proj 矩阵)
    // ---------------------------------------------------------------------

    static const int   RAY_STEPS = 128;    // DDA 最大步数
    static const float MAX_DIST = 40.0f;  // GI 作用半径(格)
    // ★ 昼夜循环:这三个不再写死,每帧由 setSunLight() 更新
    static float SUN_DIR[3] = { 0.707f, 0.707f, 0.0f };  // 指向太阳的单位向量
    static float SUN_COL[3] = { 1.05f, 1.00f, 0.95f };   // 太阳辐照
    static float SKY_COL[3] = { 0.34f, 0.38f, 0.50f };   // 天光

    // 每帧在 giFrame 之前调用:太阳方向 + 太阳/天光颜色随昼夜变化
    static void setSunLight(float dx, float dy, float dz,
                            const float sunIrr[3], const float skyIrr[3]) {
        SUN_DIR[0] = dx; SUN_DIR[1] = dy; SUN_DIR[2] = dz;
        for (int k = 0; k < 3; k++) { SUN_COL[k] = sunIrr[k]; SKY_COL[k] = skyIrr[k]; }
    }

    static const int   SPATIAL_K = 6;      // 空间复用候选数
    static const int   SPATIAL_R = 16;     // 空间复用半径(1/4分辨率像素)
    static const float HIST_MAX = 64.0f;
    static const int   SCALE = 4;      // 1/4 分辨率;太慢改 6

    int W = 0, H = 0;

    struct Reservoir {                 // 32 字节/像素
        float hitPos[3];               // 样本命中点(世界坐标)
        float hitNrm[3];
        float wSum;
        int   M;
        float pHat;                    // 样本在源像素的目标函数值
    };

    static Reservoir* resCur = nullptr, * resPrev = nullptr;
    static float* curT = nullptr, * prevT = nullptr;          // 命中距离
    static float(*curNrm)[3] = nullptr, (*prevNrm)[3] = nullptr;
    static float(*curPos)[3] = nullptr, (*prevPos)[3] = nullptr; // 命中点世界坐标
    static float* histLen = nullptr;
    static float(*accum)[3] = nullptr;                          // 时域累积
    static unsigned char(*outRGBA)[4] = nullptr;
    static unsigned char* shadowBuf = nullptr;    // R8: 1=受阳光照射, 0=遮挡

    static XMFLOAT4X4 sVP, sPrevVP, sInvVP, sPrevInvVP;
    static float sEye[3];
    static uint32_t sSeed = 12345u;

    static inline float rnd() {                    // xorshift32
        sSeed ^= sSeed << 13; sSeed ^= sSeed >> 17; sSeed ^= sSeed << 5;
        return (sSeed & 0xFFFFFF) * (1.0f / 16777216.0f);
    }
    static inline float dot3(const float* a, const float* b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }
    static inline float dot3(float ax, float ay, float az,
        float bx, float by, float bz) {
        return ax * bx + ay * by + az * bz;
    }


    void init(int rw, int rh) {
        W = rw / SCALE; H = rh / SCALE;
        size_t n = (size_t)W * H;
        resCur = new Reservoir[n];  resPrev = new Reservoir[n];
        curT = new float[n];        prevT = new float[n];
        curNrm = new float[n][3];   prevNrm = new float[n][3];
        curPos = new float[n][3];   prevPos = new float[n][3];
        histLen = new float[n]();   accum = new float[n][3]();
        outRGBA = new unsigned char[n][4];
        shadowBuf = new unsigned char[n]();
        for (size_t i = 0; i < n; i++) shadowBuf[i] = 255;
    }

    // ---- DDA 体素步进(Amanatides & Woo)----
    static bool ddaHit(const float o[3], const float d[3], float maxT,
        int* outId, float outPos[3], float outNrm[3]) {
        int x = (int)floorf(o[0]), y = (int)floorf(o[1]), z = (int)floorf(o[2]);
        int step[3]; float tMax[3], tDelta[3];
        for (int a = 0; a < 3; a++) {
            if (fabsf(d[a]) < 1e-8f) { tMax[a] = 1e30f; tDelta[a] = 0; step[a] = 0; continue; }
            step[a] = d[a] > 0 ? 1 : -1;
            float b = step[a] > 0 ? (float)(x + (a == 0) + (y + (a == 1)) + (z + (a == 2))) : (float)(a == 0 ? x : a == 1 ? y : z);
            tMax[a] = fabsf(((step[a] > 0 ? floorf(o[a]) + 1.0f : floorf(o[a])) - o[a]) / d[a]);
            tDelta[a] = fabsf(1.0f / d[a]);
        }
        int axis = -1; float t = 0;
        for (int i = 0; i < RAY_STEPS; i++) {
            axis = 0;
            if (tMax[1] < tMax[axis]) axis = 1;
            if (tMax[2] < tMax[axis]) axis = 2;
            t = tMax[axis];
            if (t > maxT) return false;
            if (axis == 0) x += step[0]; else if (axis == 1) y += step[1]; else z += step[2];
            int id = blockAt(x, y, z);
            if (id) {
                outPos[0] = o[0] + d[0] * t; outPos[1] = o[1] + d[1] * t; outPos[2] = o[2] + d[2] * t;
                outNrm[0] = outNrm[1] = outNrm[2] = 0;
                outNrm[axis] = (float)(-step[axis]);
                *outId = id;
                return true;
            }
            tMax[axis] += tDelta[axis];
        }
        return false;
    }

    // ---- 最终着色:命中点被太阳照亮吗(1 条 DDA 阴影射线)+ 天光 ----
    static void shadeHit(const float hp[3], const float hn[3], float irr[3]) {
        float so[3] = { hp[0] + hn[0] * 0.01f, hp[1] + hn[1] * 0.01f, hp[2] + hn[2] * 0.01f };
        int id; float p[3], n[3];
        bool shadow = ddaHit(so, SUN_DIR, 24.0f, &id, p, n);
        float ndl = fmaxf(dot3(hn, SUN_DIR), 0.0f);
        float sky = 0.5f + 0.5f * hn[1];
        for (int k = 0; k < 3; k++)
            irr[k] = SUN_COL[k] * (shadow ? 0.0f : ndl) + SKY_COL[k] * 0.55f * sky;

    }

    // ---- 目标函数 p̂ = || 辐照 × BRDF ||(Lambert)----
    static float evalTarget(const float irr[3], const float n[3], const float l[3]) {
        float ndl = dot3(n, l);
        if (ndl <= 0.0f) return 0.0f;
        float r[3] = { irr[0] * ndl, irr[1] * ndl, irr[2] * ndl };
        return sqrtf(dot3(r, r));
    }

    // ---- shift mapping 非对称雅可比(ReSTIR GI 核心公式)----
    static float shiftJacobian(const float hitPos[3], const float dstPos[3],
        const float srcDir[3], float srcDist, const float dstNrm[3], const float srcNrm[3]) {
        float d[3] = { hitPos[0] - dstPos[0], hitPos[1] - dstPos[1], hitPos[2] - dstPos[2] };
        float d2 = dot3(d, d);
        if (d2 < 1e-6f || srcDist < 1e-6f) return 0.0f;
        float inv = 1.0f / sqrtf(d2);
        float dirNew[3] = { d[0] * inv, d[1] * inv, d[2] * inv };
        float cosSrc = -dot3(srcDir, srcNrm);   // 源像素朝样本方向的余弦
        float cosDst = dot3(dstNrm, dirNew);   // 当前像素朝同一命中点
        if (cosSrc <= 0.0f || cosDst <= 0.0f) return 0.0f;
        return fminf((srcDist * srcDist * cosDst) / (d2 * cosSrc), 256.0f);
    }

    void setCamera(XMMATRIX viewProj, XMFLOAT3 eye) {
        sPrevVP = sVP;                                  // 上一帧矩阵自动留存
        XMStoreFloat4x4(&sVP, viewProj);
        sEye[0] = eye.x; sEye[1] = eye.y; sEye[2] = eye.z;
        XMVECTOR det; XMMATRIX inv = XMMatrixInverse(&det, XMLoadFloat4x4(&sVP));
        XMStoreFloat4x4(&sInvVP, inv);
        XMMATRIX pvp = XMLoadFloat4x4(&sPrevVP);
        if (pvp.r[0].m128_f32[0] != 0.0f) {
            XMStoreFloat4x4(&sPrevInvVP, XMMatrixInverse(&det, pvp));
        }
    }

    static inline void unproj(const XMFLOAT4X4& inv, float nx, float ny, float z, float out[3]) {
        XMVECTOR p = XMVector3TransformCoord(
            XMVectorSet(nx, ny, z, 1.0f), XMLoadFloat4x4(&inv));
        out[0] = p.m128_f32[0]; out[1] = p.m128_f32[1]; out[2] = p.m128_f32[2];
    }

    void render() {
        size_t n = (size_t)W * H;
        sSeed = 12345u + (unsigned)W;   // 每帧同种子保证确定性调试;正式版换成 frameCounter

        // ---- Pass 1: 主射线 G-buffer + 新采样 + 时域复用 ----
        for (int py = 0; py < H; py++) for (int px = 0; px < W; px++) {
            size_t i = (size_t)py * W + px;
            float nx = (px + 0.5f) / W * 2.0f - 1.0f;
            float ny = 1.0f - (py + 0.5f) / H * 2.0f;
            float o[3], farP[3];
            unproj(sInvVP, nx, ny, 0.0f, o);
            unproj(sInvVP, nx, ny, 1.0f, farP);
            float d[3] = { farP[0] - o[0], farP[1] - o[1], farP[2] - o[2] };
            float dl = sqrtf(dot3(d, d)); d[0] /= dl; d[1] /= dl; d[2] /= dl;

            int id; float hp[3], hn[3];
            bool hit = ddaHit(o, d, 512.0f, &id, hp, hn);
            float dxv = hp[0] - o[0], dyv = hp[1] - o[1], dzv = hp[2] - o[2];
            curT[i] = hit ? sqrtf(dot3(dxv, dyv, dzv, dxv, dyv, dzv)) : 1e30f;

            // ---- 太阳可见度(供 GPU 直射光/镜面高光使用) ----
            {
                float vis = 255;
                if (hit && dot3(hn, SUN_DIR) > 0.001f) {
                    float soS[3] = { hp[0] + hn[0] * 0.05f, hp[1] + hn[1] * 0.05f, hp[2] + hn[2] * 0.05f };
                    int sid; float sp[3], sn[3];
                    if (ddaHit(soS, SUN_DIR, 256.0f, &sid, sp, sn)) vis = 0;
                }
                shadowBuf[i] = (unsigned char)vis;
            }

            for (int k = 0; k < 3; k++) { curNrm[i][k] = hn[k]; curPos[i][k] = hp[k]; }

            // 余弦加权半球采样新样本
            Reservoir& R = resCur[i]; R.wSum = 0; R.M = 0; R.pHat = 0;
            if (hit) {
                float albedo[3]; blockColor(id, albedo);
                float u = rnd(), v = rnd();
                float phi = 6.2831853f * u, r2 = v;
                float sr = sqrtf(r2);
                float t1[3], t2[3];   // 法线正交基
                float up[3] = { fabsf(hn[1]) < 0.9f ? 0.f : 1.f, fabsf(hn[1]) < 0.9f ? 1.f : 0.f, 0.f };
                float c1[3] = { hn[1] * up[2] - hn[2] * up[1], hn[2] * up[0] - hn[0] * up[2], hn[0] * up[1] - hn[1] * up[0] };
                float cl = sqrtf(dot3(c1, c1)); for (int k = 0; k < 3; k++) t1[k] = c1[k] / cl;
                float t2v[3] = { hn[1] * t1[2] - hn[2] * t1[1], hn[2] * t1[0] - hn[0] * t1[2], hn[0] * t1[1] - hn[1] * t1[0] };
                for (int k = 0; k < 3; k++) t2[k] = t2v[k];
                float dir[3] = {
                    t1[0] * cosf(phi) * sr + t2[0] * sinf(phi) * sr + hn[0] * sqrtf(fmaxf(0.f,1.f - r2)),
                    t1[1] * cosf(phi) * sr + t2[1] * sinf(phi) * sr + hn[1] * sqrtf(fmaxf(0.f,1.f - r2)),
                    t1[2] * cosf(phi) * sr + t2[2] * sinf(phi) * sr + hn[2] * sqrtf(fmaxf(0.f,1.f - r2)) };
                int id2; float hp2[3], hn2[3];
                float so[3] = { hp[0] + hn[0] * 0.01f, hp[1] + hn[1] * 0.01f, hp[2] + hn[2] * 0.01f };
                float irr2[3];
                if (ddaHit(so, dir, MAX_DIST, &id2, hp2, hn2)) {
                    float a2[3]; blockColor(id2, a2);
                    // 粗估辐照(无阴影版,仅供 p̂;最终着色在 Pass2 带阴影)
                    float ndl2 = fmaxf(dot3(hn2, SUN_DIR), 0.f);
                    for (int k = 0; k < 3; k++) irr2[k] = a2[k] * (SUN_COL[k] * ndl2 + SKY_COL[k] * 0.35f);
                    float ex = hp2[0] - hp[0], ey = hp2[1] - hp[1], ez = hp2[2] - hp[2];
                    float dist2 = sqrtf(dot3(ex, ey, ez, ex, ey, ez));

                    float pHat = evalTarget(irr2, hn, dir);
                    if (pHat > 0) {
                        float wi = 3.14159f * pHat;   // cosine pdf 反演
                        R.wSum += wi; R.M++; R.pHat = pHat;
                        for (int k = 0; k < 3; k++) { R.hitPos[k] = hp2[k]; R.hitNrm[k] = hn2[k]; }
                    }
                }
                // ---- 时域复用:重投影上一帧样本 ----
                bool reprojOK = false;
                if (prevT[i] < 1e29f) {
                    XMVECTOR pc = XMVector3Transform(XMVectorSet(prevPos[i][0], prevPos[i][1], prevPos[i][2], 1.f),
                        XMLoadFloat4x4(&sVP));
                    float w = pc.m128_f32[3];
                    if (w > 0.001f) {
                        float pnx = pc.m128_f32[0] / w, pny = pc.m128_f32[1] / w;
                        if (fabsf(pnx) < 1.f && fabsf(pny) < 1.f) {
                            int qx = (int)((pnx * 0.5f + 0.5f) * W), qy = (int)((0.5f - pny * 0.5f) * H);
                            if (qx >= 0 && qx < W && qy >= 0 && qy < H) {
                                size_t j = (size_t)qy * W + qx;
                                float nd = dot3(prevNrm[j], curNrm[i]);
                                float dt = fabsf(prevT[j] - curT[i]);
                                if (nd > 0.5f && dt < 1.5f && resPrev[j].M > 0) {
                                    // 用上一帧样本在当前像素重新评估 p̂
                                    float sd[3] = { resPrev[j].hitPos[0] - hp[0],
                                                    resPrev[j].hitPos[1] - hp[1],
                                                    resPrev[j].hitPos[2] - hp[2] };
                                    float sdist = sqrtf(dot3(sd, sd));
                                    if (sdist > 1e-4f && sdist < MAX_DIST * 1.5f) {
                                        sd[0] /= sdist; sd[1] /= sdist; sd[2] /= sdist;
                                        float irrPrev[3]; blockColor(0, irrPrev); // 占位:近似用太阳+天光
                                        float ndl = fmaxf(dot3(resPrev[j].hitNrm, SUN_DIR), 0.f);
                                        for (int k = 0; k < 3; k++) irrPrev[k] = SUN_COL[k] * ndl + SKY_COL[k] * 0.35f;
                                        float pHat = evalTarget(irrPrev, hn, sd);
                                        if (pHat > 0) {
                                            R.wSum += pHat * resPrev[j].M; R.M += resPrev[j].M;
                                            if (rnd() * R.wSum < pHat * resPrev[j].M) {
                                                for (int k = 0; k < 3; k++) { R.hitPos[k] = resPrev[j].hitPos[k]; R.hitNrm[k] = resPrev[j].hitNrm[k]; }
                                                R.pHat = pHat;
                                            }
                                            reprojOK = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                histLen[i] = reprojOK ? fminf(histLen[i] + 1.f, HIST_MAX) : 0.f;
            }
            else { histLen[i] = 0; R.M = 0; }
        }

        // ---- Pass 2: 空间复用 + 最终着色 + 时域累积 + 输出 ----
        for (int py = 0; py < H; py++) for (int px = 0; px < W; px++) {
            size_t i = (size_t)py * W + px;
            float irr[3] = { SKY_COL[0], SKY_COL[1], SKY_COL[2] };
            float alpha = 0.0f;
            if (curT[i] < 1e29f) {
                alpha = 1.0f;
                Reservoir R = resCur[i];
                float dstPos[3] = { curPos[i][0],curPos[i][1],curPos[i][2] };
                // 空间复用:K 个邻域 reservoir,带雅可比重算
                for (int c = 0; c < SPATIAL_K; c++) {
                    int qx = px + (int)((rnd() * 2 - 1) * SPATIAL_R);
                    int qy = py + (int)((rnd() * 2 - 1) * SPATIAL_R);
                    if (qx < 0 || qx >= W || qy < 0 || qy >= H) continue;
                    size_t j = (size_t)qy * W + qx;
                    if (resCur[j].M == 0) continue;
                    float nd = dot3(curNrm[j], curNrm[i]);
                    if (nd < 0.5f) continue;
                    float px2 = dstPos[0] - curPos[j][0], py2 = dstPos[1] - curPos[j][1], pz2 = dstPos[2] - curPos[j][2];
                    float pd = sqrtf(dot3(px2, py2, pz2, px2, py2, pz2));

                    if (pd > 4.0f) continue;   // 平面距离门槛(对应原版 threshold)
                    // 样本方向重指向当前像素
                    float sd[3] = { resCur[j].hitPos[0] - dstPos[0],
                                    resCur[j].hitPos[1] - dstPos[1],
                                    resCur[j].hitPos[2] - dstPos[2] };
                    float sdist = sqrtf(dot3(sd, sd));
                    if (sdist<1e-4f || sdist>MAX_DIST * 1.5f) continue;
                    sd[0] /= sdist; sd[1] /= sdist; sd[2] /= sdist;
                    // 邻域原始方向(从邻域像素到其命中点)
                    float od[3] = { resCur[j].hitPos[0] - curPos[j][0],
                                    resCur[j].hitPos[1] - curPos[j][1],
                                    resCur[j].hitPos[2] - curPos[j][2] };
                    float odist = sqrtf(dot3(od, od)); if (odist < 1e-4f) continue;
                    od[0] /= odist; od[1] /= odist; od[2] /= odist;
                    float J = shiftJacobian(resCur[j].hitPos, dstPos, od, odist, curNrm[i], curNrm[j]);
                    if (J <= 0) continue;
                    float irrJ[3];   // 目标函数近似(无阴影粗估)
                    float ndl = fmaxf(dot3(resCur[j].hitNrm, SUN_DIR), 0.f);
                    for (int k = 0; k < 3; k++) irrJ[k] = SUN_COL[k] * ndl + SKY_COL[k] * 0.35f;
                    float pHatDst = evalTarget(irrJ, curNrm[i], sd) * J;
                    if (pHatDst <= 0) continue;
                    float wi = pHatDst / fmaxf(resCur[j].pHat, 1e-4f);
                    float oldWSum = R.wSum;
                    R.wSum += wi; R.M++;
                    if (rnd() * R.wSum < wi) {
                        for (int k = 0; k < 3; k++) { R.hitPos[k] = resCur[j].hitPos[k]; R.hitNrm[k] = resCur[j].hitNrm[k]; }
                        R.pHat = pHatDst;
                    }
                }
                // 输出权重(稳定版:钳制,抑制重用爆炸)
                float Wgt = (R.M > 0) ? R.wSum / R.M / fmaxf(R.pHat, 1e-4f) : 0.0f;
                Wgt = fminf(fmaxf(Wgt, 0.0f), 3.0f);
                if (R.M > 0) {
                    float sh[3]; shadeHit(R.hitPos, R.hitNrm, sh);   // 只对选中样本着色——ReSTIR 的省钱本质
                    for (int k = 0; k < 3; k++) irr[k] = sh[k] * Wgt;
                }
                // 时域累积降噪
                float a = 1.0f / (1.0f + histLen[i]);
                for (int k = 0; k < 3; k++) accum[i][k] = accum[i][k] * (1.0f - a) + irr[k] * a;
                for (int k = 0; k < 3; k++) irr[k] = accum[i][k];
            }
            else {
                for (int k = 0; k < 3; k++) accum[i][k] = irr[k];   // 天空直接过
            }
            const float GAIN = 1.4f;   // GI 输出增益:画面平就升,有白斑就降
            outRGBA[i][0] = (unsigned char)fminf(irr[0] * GAIN, 1.f) * 255;
            outRGBA[i][1] = (unsigned char)fminf(irr[1] * GAIN, 1.f) * 255;
            outRGBA[i][2] = (unsigned char)fminf(irr[2] * GAIN, 1.f) * 255;
            outRGBA[i][3] = (unsigned char)(alpha * 255);

        }
        // 交换历史
        Reservoir* tr = resCur; resCur = resPrev; resPrev = tr;
        float* tf = curT; curT = prevT; prevT = tf;
        float(*tn)[3] = curNrm; curNrm = prevNrm; prevNrm = tn;
        float(*tp)[3] = curPos; curPos = prevPos; prevPos = tp;
    }
} // namespace cigi


static void initBlockInfo() {
    for (int i = 0; i < BLOCK_COUNT; i++)
        g_blockInfo[i] = { 128, 128, 128, false, false, true,
                           {TEX_NONE, TEX_NONE, TEX_NONE, TEX_NONE, TEX_NONE, TEX_NONE} };

    auto set = [](Block b, uint8_t r, uint8_t g, uint8_t bl,
                  bool trans = false, bool liq = false, bool sol = true) {
        g_blockInfo[(int)b] = { r, g, bl, trans, liq, sol,
                                {TEX_NONE, TEX_NONE, TEX_NONE, TEX_NONE, TEX_NONE, TEX_NONE} };
    };
    auto setTexAll = [](Block b, uint8_t layer) {
        for (int i = 0; i < 6; i++) g_blockInfo[(int)b].tex[i] = layer;
    };
    auto setTexFaces = [](Block b, uint8_t top, uint8_t bottom, uint8_t side) {
        g_blockInfo[(int)b].tex[0] = side;
        g_blockInfo[(int)b].tex[1] = side;
        g_blockInfo[(int)b].tex[2] = top;
        g_blockInfo[(int)b].tex[3] = bottom;
        g_blockInfo[(int)b].tex[4] = side;
        g_blockInfo[(int)b].tex[5] = side;
    };

    // MISSING(未实现方块兜底):纯白基色,网格着色时按世界坐标翻成黑白棋盘格
    g_blockInfo[(int)Block::MISSING] = { 255, 255, 255, false, false, true };  // tex 默认全 0




    set(Block::AIR, 0,0,0, true, false, false);
    set(Block::STONE,         130,134,128);
    set(Block::DEEPSLATE,      64, 64, 68);
    set(Block::TUFF,           90, 88, 84);
    set(Block::CALCITE,       200,198,192);
    set(Block::ANDESITE,      120,120,122);
    set(Block::DIORITE,       190,190,188);
    set(Block::GRANITE,       160,130,120);
    set(Block::DIRT,           95, 70, 50);
    set(Block::GRASS,         114,173, 78);
    set(Block::PODZOL,         75, 60, 40);
    set(Block::MYCELIUM,      140,120,130);
    set(Block::MUD,            60, 50, 40);
    set(Block::SAND,          218,210,170);
    set(Block::RED_SAND,      190,110, 60);
    set(Block::GRAVEL,        130,128,126);
    set(Block::CLAY,          160,160,175);
    set(Block::SANDSTONE,     220,210,170);
    set(Block::RED_SANDSTONE, 200,120, 70);
    set(Block::TERRACOTTA,    180,100, 80);
    set(Block::SNOW_BLOCK,    240,245,250);
    set(Block::ICE,           170,200,230, true);
    set(Block::PACKED_ICE,    140,180,220);
    set(Block::BLUE_ICE,      120,160,220, true);
    set(Block::WATER,          59,124,191, true, true, false);
    set(Block::LAVA,          230,120, 30, false, true, false);
    set(Block::COAL_ORE,       40, 40, 40);
    set(Block::IRON_ORE,      200,160,120);
    set(Block::COPPER_ORE,    200,130, 80);
    set(Block::GOLD_ORE,      240,210, 70);
    set(Block::REDSTONE_ORE,  200, 40, 40);
    set(Block::LAPIS_ORE,      40, 70,180);
    set(Block::DIAMOND_ORE,    90,220,220);
    set(Block::EMERALD_ORE,    60,200,110);
    set(Block::OAK_LOG,       107, 79, 58);
    set(Block::OAK_LEAVES,     57,112, 44, true);
    set(Block::SPRUCE_LOG,     90, 70, 50);
    set(Block::SPRUCE_LEAVES,  45, 90, 45, true);
    set(Block::BIRCH_LOG,     200,190,170);
    set(Block::BIRCH_LEAVES,   90,150, 70, true);
    set(Block::JUNGLE_LOG,     90, 65, 45);
    set(Block::JUNGLE_LEAVES,  60,130, 50, true);
    set(Block::ACACIA_LOG,    120, 90, 60);
    set(Block::ACACIA_LEAVES,  90,130, 60, true);
    set(Block::DARK_OAK_LOG,   80, 60, 45);
    set(Block::DARK_OAK_LEAVES,50,100, 50, true);
    set(Block::CACTUS,         70,130, 70, true);
    set(Block::DEAD_BUSH,     120, 90, 60, true);
    set(Block::SHORT_GRASS,    80,160, 60, true);
    set(Block::TALL_GRASS,     90,170, 70, true);
    set(Block::FERN,           60,130, 70, true);
    set(Block::MOSS_CARPET,    70,110, 60, true);
    set(Block::MUSHROOM_RED,  200, 60, 60, true);
    set(Block::MUSHROOM_BROWN,160,120, 80, true);
    set(Block::SUGAR_CANE,    150,190,100, true);
    set(Block::BAMBOO,         90,170, 80, true);
    set(Block::VINE,           60,110, 60, true);
    set(Block::GLASS,         180,200,220, true);
    set(Block::BRICK,         150, 80, 60);
    set(Block::PLANKS,        160,120, 80);

    setTexAll(Block::STONE,     TEX_STONE);
    setTexAll(Block::DEEPSLATE, TEX_DEEPSLATE);
    setTexAll(Block::TUFF,      TEX_TUFF);
    setTexAll(Block::CALCITE,   TEX_CALCITE);
    setTexAll(Block::ANDESITE,  TEX_ANDESITE);
    setTexAll(Block::DIORITE,   TEX_DIORITE);
    setTexAll(Block::GRANITE,   TEX_GRANITE);

    setTexAll(Block::DIRT,      TEX_DIRT);
    setTexFaces(Block::GRASS,     TEX_GRASS_TOP, TEX_DIRT, TEX_DIRT);
    setTexFaces(Block::PODZOL,    TEX_PODZOL_TOP, TEX_DIRT, TEX_PODZOL_SIDE);
    setTexAll(Block::MYCELIUM,  TEX_MYCELIUM);
    setTexAll(Block::MUD,       TEX_MUD);

    setTexAll(Block::SAND,           TEX_SAND);
    setTexAll(Block::RED_SAND,       TEX_RED_SAND);
    setTexAll(Block::GRAVEL,         TEX_GRAVEL);
    setTexAll(Block::CLAY,           TEX_CLAY);
    setTexAll(Block::SANDSTONE,      TEX_SANDSTONE);
    setTexAll(Block::RED_SANDSTONE,  TEX_RED_SANDSTONE);
    setTexAll(Block::TERRACOTTA,     TEX_TERRACOTTA);

    setTexFaces(Block::OAK_LOG,      TEX_LOG_TOP, TEX_LOG_TOP, TEX_LOG_SIDE);
    setTexFaces(Block::SPRUCE_LOG,   TEX_LOG_TOP, TEX_LOG_TOP, TEX_LOG_SIDE);
    setTexFaces(Block::BIRCH_LOG,    TEX_LOG_TOP, TEX_LOG_TOP, TEX_LOG_SIDE);
    setTexFaces(Block::JUNGLE_LOG,   TEX_LOG_TOP, TEX_LOG_TOP, TEX_LOG_SIDE);
    setTexFaces(Block::ACACIA_LOG,   TEX_LOG_TOP, TEX_LOG_TOP, TEX_LOG_SIDE);
    setTexFaces(Block::DARK_OAK_LOG, TEX_LOG_TOP, TEX_LOG_TOP, TEX_LOG_SIDE);
    setTexAll(Block::PLANKS,         TEX_PLANKS);

    setTexAll(Block::COAL_ORE,     TEX_COAL_ORE);
    setTexAll(Block::IRON_ORE,     TEX_IRON_ORE);
    setTexAll(Block::COPPER_ORE,   TEX_COPPER_ORE);
    setTexAll(Block::GOLD_ORE,     TEX_GOLD_ORE);
    setTexAll(Block::REDSTONE_ORE, TEX_REDSTONE_ORE);
    setTexAll(Block::LAPIS_ORE,    TEX_LAPIS_ORE);
    setTexAll(Block::DIAMOND_ORE,  TEX_DIAMOND_ORE);
    setTexAll(Block::EMERALD_ORE,  TEX_EMERALD_ORE);
}

inline bool isTransparent(Block b) { return g_blockInfo[(int)b].transparent; }
inline bool isOpaqueBlock(Block b) { return b != Block::AIR && !isTransparent(b); }
inline bool isSolidBlock(Block b) {
    return b != Block::AIR && b != Block::WATER && b != Block::LAVA && !isTransparent(b);
}

static const Block g_selectableBlocks[] = {
    Block::STONE, Block::DEEPSLATE, Block::TUFF, Block::CALCITE,
    Block::ANDESITE, Block::DIORITE, Block::GRANITE,
    Block::DIRT, Block::GRASS, Block::PODZOL, Block::MYCELIUM, Block::MUD,
    Block::SAND, Block::RED_SAND, Block::GRAVEL, Block::CLAY,
    Block::SANDSTONE, Block::RED_SANDSTONE, Block::TERRACOTTA,
    Block::SNOW_BLOCK, Block::ICE, Block::PACKED_ICE, Block::BLUE_ICE,
    Block::WATER, Block::LAVA,
    Block::COAL_ORE, Block::IRON_ORE, Block::COPPER_ORE, Block::GOLD_ORE,
    Block::REDSTONE_ORE, Block::LAPIS_ORE, Block::DIAMOND_ORE, Block::EMERALD_ORE,
    Block::OAK_LOG, Block::OAK_LEAVES, Block::SPRUCE_LOG, Block::SPRUCE_LEAVES,
    Block::BIRCH_LOG, Block::BIRCH_LEAVES, Block::JUNGLE_LOG, Block::JUNGLE_LEAVES,
    Block::ACACIA_LOG, Block::ACACIA_LEAVES, Block::DARK_OAK_LOG, Block::DARK_OAK_LEAVES,
    Block::CACTUS, Block::DEAD_BUSH, Block::SHORT_GRASS, Block::TALL_GRASS,
    Block::FERN, Block::MOSS_CARPET, Block::MUSHROOM_RED, Block::MUSHROOM_BROWN,
    Block::SUGAR_CANE, Block::BAMBOO, Block::VINE,
    Block::GLASS, Block::BRICK, Block::PLANKS,
};
static const int g_selectableCount =
    (int)(sizeof(g_selectableBlocks) / sizeof(g_selectableBlocks[0]));

static const wchar_t* blockNameCN(Block b) {
    switch (b) {
        case Block::STONE:           return L"石头";
        case Block::DEEPSLATE:       return L"深板岩";
        case Block::TUFF:            return L"凝灰岩";
        case Block::CALCITE:         return L"方钙石";
        case Block::ANDESITE:        return L"安山岩";
        case Block::DIORITE:         return L"闪长岩";
        case Block::GRANITE:         return L"花岗岩";
        case Block::DIRT:            return L"泥土";
        case Block::GRASS:           return L"草方块";
        case Block::PODZOL:          return L"灰化土";
        case Block::MYCELIUM:        return L"菌丝体";
        case Block::MUD:             return L"泥巴";
        case Block::SAND:            return L"沙子";
        case Block::RED_SAND:        return L"红沙";
        case Block::GRAVEL:          return L"沙砾";
        case Block::CLAY:            return L"黏土";
        case Block::SANDSTONE:       return L"砂岩";
        case Block::RED_SANDSTONE:   return L"红砂岩";
        case Block::TERRACOTTA:      return L"陶瓦";
        case Block::SNOW_BLOCK:      return L"雪块";
        case Block::ICE:             return L"冰";
        case Block::PACKED_ICE:      return L"浮冰";
        case Block::BLUE_ICE:        return L"蓝冰";
        case Block::WATER:           return L"水";
        case Block::LAVA:            return L"岩浆";
        case Block::COAL_ORE:        return L"煤矿石";
        case Block::IRON_ORE:        return L"铁矿石";
        case Block::COPPER_ORE:      return L"铜矿石";
        case Block::GOLD_ORE:        return L"金矿石";
        case Block::REDSTONE_ORE:    return L"红石矿石";
        case Block::LAPIS_ORE:       return L"青金石矿石";
        case Block::DIAMOND_ORE:     return L"钻石矿石";
        case Block::EMERALD_ORE:     return L"绿宝石矿石";
        case Block::OAK_LOG:         return L"橡木原木";
        case Block::OAK_LEAVES:      return L"橡树树叶";
        case Block::SPRUCE_LOG:      return L"云杉原木";
        case Block::SPRUCE_LEAVES:   return L"云杉树叶";
        case Block::BIRCH_LOG:       return L"白桦原木";
        case Block::BIRCH_LEAVES:    return L"白桦树叶";
        case Block::JUNGLE_LOG:      return L"丛林原木";
        case Block::JUNGLE_LEAVES:   return L"丛林树叶";
        case Block::ACACIA_LOG:      return L"金合欢原木";
        case Block::ACACIA_LEAVES:   return L"金合欢树叶";
        case Block::DARK_OAK_LOG:    return L"深色橡木原木";
        case Block::DARK_OAK_LEAVES: return L"深色橡木树叶";
        case Block::CACTUS:          return L"仙人掌";
        case Block::DEAD_BUSH:       return L"枯萎的灌木";
        case Block::SHORT_GRASS:     return L"短草";
        case Block::TALL_GRASS:      return L"高草";
        case Block::FERN:            return L"蕨";
        case Block::MOSS_CARPET:     return L"苔藓地毯";
        case Block::MUSHROOM_RED:    return L"红色蘑菇";
        case Block::MUSHROOM_BROWN:  return L"棕色蘑菇";
        case Block::SUGAR_CANE:      return L"甘蔗";
        case Block::BAMBOO:          return L"竹子";
        case Block::VINE:            return L"藤蔓";
        case Block::GLASS:           return L"玻璃";
        case Block::BRICK:           return L"砖块";
        case Block::PLANKS:          return L"木板";
    }
    return L"?";
}

// ============================================================
// Section 3. 噪声
// ============================================================
inline uint32_t hashU32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
inline uint32_t hash3(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed;
    h = hashU32(h ^ (uint32_t)x * 374761393u);
    h = hashU32(h ^ (uint32_t)y * 668265263u);
    h = hashU32(h ^ (uint32_t)z * 2147483647u);
    return h;
}
inline float hashToFloat(uint32_t h) { return (h & 0xFFFFFF) / (float)0x1000000; }
inline float rand01_3(int x, int y, int z, uint32_t seed) {
    return hashToFloat(hash3(x, y, z, seed));
}

class Noise2D {
    uint32_t seed;
public:
    explicit Noise2D(uint32_t s = 0) : seed(s) {}
    void reseed(uint32_t s) { seed = s; }
    float sample(float x, float z) const {
        int xi = (int)floorf(x), zi = (int)floorf(z);
        float xf = x - xi, zf = z - zi;
        float u = xf*xf*(3-2*xf);
        float v = zf*zf*(3-2*zf);
        float c00 = rand01_3(xi, 0, zi, seed);
        float c10 = rand01_3(xi+1, 0, zi, seed);
        float c01 = rand01_3(xi, 0, zi+1, seed);
        float c11 = rand01_3(xi+1, 0, zi+1, seed);
        float x00 = c00 + (c10-c00)*u;
        float x01 = c01 + (c11-c01)*u;
        return x00 + (x01-x00)*v;
    }
    float fbm(float x, float z, int oct, float lac = 2.0f, float gain = 0.5f) const {
        float sum=0, amp=1, freq=1, norm=0;
        for (int i = 0; i < oct; i++) {
            sum += sample(x*freq, z*freq) * amp;
            norm += amp; amp *= gain; freq *= lac;
        }
        return sum / norm;
    }
    float ridged(float x, float z, int oct) const {
        float sum=0, amp=1, freq=1, norm=0;
        for (int i = 0; i < oct; i++) {
            float n = 1.0f - fabsf(sample(x*freq, z*freq)*2.0f - 1.0f);
            sum += n*n*amp; norm += amp; amp *= 0.5f; freq *= 2.0f;
        }
        return sum / norm;
    }
    void domainWarp(float& x, float& z, float strength) const {
        float wx = sample(x*0.5f + 17.3f, z*0.5f + 41.9f)*2.0f - 1.0f;
        float wz = sample(x*0.5f - 23.1f, z*0.5f + 7.7f)*2.0f - 1.0f;
        x += wx * strength;
        z += wz * strength;
    }
};

class Noise3D {
    uint32_t seed;
public:
    explicit Noise3D(uint32_t s = 0) : seed(s) {}
    void reseed(uint32_t s) { seed = s; }
    float sample(float x, float y, float z) const {
        int xi = (int)floorf(x), yi = (int)floorf(y), zi = (int)floorf(z);
        float xf = x - xi, yf = y - yi, zf = z - zi;
        float u = xf*xf*(3-2*xf);
        float v = yf*yf*(3-2*yf);
        float w = zf*zf*(3-2*zf);
        auto L = [](float a, float b, float t){ return a+(b-a)*t; };
        float c000 = rand01_3(xi,   yi,   zi,   seed);
        float c100 = rand01_3(xi+1, yi,   zi,   seed);
        float c010 = rand01_3(xi,   yi+1, zi,   seed);
        float c110 = rand01_3(xi+1, yi+1, zi,   seed);
        float c001 = rand01_3(xi,   yi,   zi+1, seed);
        float c101 = rand01_3(xi+1, yi,   zi+1, seed);
        float c011 = rand01_3(xi,   yi+1, zi+1, seed);
        float c111 = rand01_3(xi+1, yi+1, zi+1, seed);
        float x00 = L(c000,c100,u), x10 = L(c010,c110,u);
        float x01 = L(c001,c101,u), x11 = L(c011,c111,u);
        return L(L(x00,x10,v), L(x01,x11,v), w);
    }
    float fbm(float x, float y, float z, int oct) const {
        float sum=0, amp=1, freq=1, norm=0;
        for (int i = 0; i < oct; i++) {
            sum += sample(x*freq, y*freq, z*freq) * amp;
            norm += amp; amp *= 0.5f; freq *= 2.0f;
        }
        return sum / norm;
    }
};

class Rng {
    uint64_t s;
public:
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {
        for (int i = 0; i < 12; i++) next();
    }
    inline uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    inline int range(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + (int)(next() % (uint64_t)(hi - lo + 1));
    }
    inline float f01() { return (next() >> 40) * (1.0f / (1 << 24)); }
};

// ============================================================
// Section 4. 生物群系
// ============================================================
enum class Biome : uint8_t {
    OCEAN, DEEP_OCEAN, WARM_OCEAN, FROZEN_OCEAN,
    RIVER, FROZEN_RIVER, BEACH, SNOWY_BEACH, STONY_SHORE,
    PLAINS, SUNFLOWER_PLAINS, SNOWY_PLAINS,
    FOREST, FLOWER_FOREST, BIRCH_FOREST, DARK_FOREST,
    TAIGA, SNOWY_TAIGA, OLD_GROWTH_TAIGA,
    JUNGLE, SPARSE_JUNGLE, BAMBOO_JUNGLE,
    DESERT, SAVANNA, SAVANNA_PLATEAU, WINDSWEPT_SAVANNA,
    BADLANDS, ERODED_BADLANDS, WOODED_BADLANDS,
    SWAMP, MANGROVE_SWAMP,
    MOUNTAINS, SNOWY_MOUNTAINS, STONY_PEAKS,
    MEADOW, GROVE, SNOWY_SLOPES, JAGGED_PEAKS, FROZEN_PEAKS,
    MUSHROOM_FIELDS,
    COUNT
};

struct BiomeProfile {
    Block surface, subsurface, underwater;
    int baseHeight, heightVariation;
    bool snowy, forest, ocean, mountain;
};

static BiomeProfile biomeProfile(Biome b) {
    switch (b) {
        case Biome::OCEAN:            return {Block::GRAVEL, Block::SAND, Block::GRAVEL, 38, 3, false, false, true, false};
        case Biome::DEEP_OCEAN:       return {Block::GRAVEL, Block::SAND, Block::GRAVEL, 26, 4, false, false, true, false};
        case Biome::WARM_OCEAN:       return {Block::SAND, Block::SAND, Block::SAND, 42, 3, false, false, true, false};
        case Biome::FROZEN_OCEAN:     return {Block::GRAVEL, Block::SAND, Block::GRAVEL, 38, 3, true, false, true, false};
        case Biome::RIVER:            return {Block::SAND, Block::SAND, Block::SAND, 56, 2, false, false, false, false};
        case Biome::FROZEN_RIVER:     return {Block::SAND, Block::SAND, Block::GRAVEL, 56, 2, true, false, false, false};
        case Biome::BEACH:            return {Block::SAND, Block::SAND, Block::SAND, 66, 1, false, false, false, false};
        case Biome::SNOWY_BEACH:      return {Block::SNOW_BLOCK, Block::SAND, Block::SAND, 66, 1, true, false, false, false};
        case Biome::STONY_SHORE:      return {Block::STONE, Block::STONE, Block::GRAVEL, 66, 3, false, false, false, false};
        case Biome::PLAINS:           return {Block::GRASS, Block::DIRT, Block::DIRT, 72, 3, false, false, false, false};
        case Biome::SUNFLOWER_PLAINS: return {Block::GRASS, Block::DIRT, Block::DIRT, 72, 3, false, false, false, false};
        case Biome::SNOWY_PLAINS:     return {Block::SNOW_BLOCK, Block::DIRT, Block::DIRT, 72, 3, true, false, false, false};
        case Biome::FOREST:           return {Block::GRASS, Block::DIRT, Block::DIRT, 75, 5, false, true, false, false};
        case Biome::FLOWER_FOREST:    return {Block::GRASS, Block::DIRT, Block::DIRT, 75, 5, false, true, false, false};
        case Biome::BIRCH_FOREST:     return {Block::GRASS, Block::DIRT, Block::DIRT, 75, 5, false, true, false, false};
        case Biome::DARK_FOREST:      return {Block::GRASS, Block::DIRT, Block::DIRT, 74, 5, false, true, false, false};
        case Biome::TAIGA:            return {Block::GRASS, Block::DIRT, Block::DIRT, 77, 7, false, true, false, false};
        case Biome::SNOWY_TAIGA:      return {Block::SNOW_BLOCK, Block::DIRT, Block::DIRT, 77, 7, true, true, false, false};
        case Biome::OLD_GROWTH_TAIGA: return {Block::PODZOL, Block::DIRT, Block::DIRT, 77, 7, false, true, false, false};
        case Biome::JUNGLE:           return {Block::GRASS, Block::DIRT, Block::DIRT, 78, 8, false, true, false, false};
        case Biome::SPARSE_JUNGLE:    return {Block::GRASS, Block::DIRT, Block::DIRT, 76, 7, false, true, false, false};
        case Biome::BAMBOO_JUNGLE:    return {Block::GRASS, Block::DIRT, Block::DIRT, 76, 6, false, true, false, false};
        case Biome::DESERT:           return {Block::SAND, Block::SANDSTONE, Block::SAND, 72, 4, false, false, false, false};
        case Biome::SAVANNA:          return {Block::GRASS, Block::DIRT, Block::DIRT, 75, 5, false, false, false, false};
        case Biome::SAVANNA_PLATEAU:  return {Block::GRASS, Block::DIRT, Block::DIRT, 79, 7, false, false, false, false};
        case Biome::WINDSWEPT_SAVANNA:return {Block::GRASS, Block::DIRT, Block::DIRT, 82, 10, false, false, false, false};
        case Biome::BADLANDS:         return {Block::RED_SAND, Block::TERRACOTTA, Block::RED_SAND, 75, 10, false, false, false, false};
        case Biome::ERODED_BADLANDS:  return {Block::RED_SAND, Block::TERRACOTTA, Block::RED_SAND, 77, 14, false, false, false, false};
        case Biome::WOODED_BADLANDS:  return {Block::RED_SAND, Block::TERRACOTTA, Block::RED_SAND, 77, 10, false, true, false, false};
        case Biome::SWAMP:            return {Block::GRASS, Block::DIRT, Block::CLAY, 64, 2, false, false, false, false};
        case Biome::MANGROVE_SWAMP:   return {Block::MUD, Block::MUD, Block::CLAY, 64, 2, false, false, false, false};
        case Biome::MOUNTAINS:        return {Block::GRASS, Block::DIRT, Block::DIRT, 85, 14, false, false, false, true};
        case Biome::SNOWY_MOUNTAINS:  return {Block::SNOW_BLOCK, Block::DIRT, Block::DIRT, 88, 14, true, false, false, true};
        case Biome::STONY_PEAKS:      return {Block::STONE, Block::STONE, Block::GRAVEL, 100, 20, false, false, false, true};
        case Biome::MEADOW:           return {Block::GRASS, Block::DIRT, Block::DIRT, 95, 12, false, false, false, true};
        case Biome::GROVE:            return {Block::SNOW_BLOCK, Block::DIRT, Block::DIRT, 95, 14, true, true, false, true};
        case Biome::SNOWY_SLOPES:     return {Block::SNOW_BLOCK, Block::DIRT, Block::DIRT, 100, 18, true, false, false, true};
        case Biome::JAGGED_PEAKS:     return {Block::STONE, Block::STONE, Block::STONE, 108, 24, true, false, false, true};
        case Biome::FROZEN_PEAKS:     return {Block::PACKED_ICE, Block::STONE, Block::STONE, 108, 24, true, false, false, true};
        case Biome::MUSHROOM_FIELDS: return { Block::MYCELIUM, Block::DIRT, Block::DIRT, 74, 6, false, false, false, false };

        default:
            return { Block::MISSING, Block::STONE, Block::STONE, 72, 3, false, false, false, false };
    }        // ← switch 的收尾,且只有这一个
}

//}

//}

struct Climate {
    float temperature, humidity, continentalness, erosion, weirdness;
};

static inline float widen01(float v, float scale) {
    float s = (v - 0.5f) * scale + 0.5f;
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    return s;
}

static Biome selectBiome(const Climate& c) {
    if (c.continentalness < 0.12f) {
        if (c.continentalness < 0.05f) return Biome::DEEP_OCEAN;
        if (c.temperature < 0.15f)     return Biome::FROZEN_OCEAN;
        if (c.temperature > 0.72f)     return Biome::WARM_OCEAN;
        return Biome::OCEAN;
    }
    if (c.continentalness < 0.20f) {
        if (c.temperature < 0.15f) return Biome::SNOWY_BEACH;
        if (c.erosion > 0.72f)     return Biome::STONY_SHORE;
        return Biome::BEACH;
    }
    if (c.weirdness > 0.90f && c.continentalness < 0.35f)
        return (c.temperature < 0.15f) ? Biome::FROZEN_RIVER : Biome::RIVER;
    if (c.erosion > 0.62f && c.continentalness > 0.42f) {
        if (c.temperature < 0.1f) {
            if (c.erosion > 0.94f) return Biome::JAGGED_PEAKS;
            if (c.erosion > 0.88f) return Biome::FROZEN_PEAKS;
            return Biome::SNOWY_MOUNTAINS;
        }
        if (c.erosion > 0.94f) return Biome::STONY_PEAKS;
        if (c.temperature < 0.4f) return Biome::GROVE;
        return Biome::MOUNTAINS;
    }
    if (c.erosion > 0.75f && c.temperature > 0.35f && c.temperature < 0.6f)
        return Biome::MEADOW;
    if (c.temperature < 0.15f) {
        if (c.humidity > 0.6f) return Biome::SNOWY_TAIGA;
        return Biome::SNOWY_PLAINS;
    }
    if (c.temperature < 0.35f) {
        if (c.humidity > 0.7f) return Biome::OLD_GROWTH_TAIGA;
        if (c.humidity > 0.5f) return Biome::TAIGA;
        if (c.continentalness > 0.7f) return Biome::WINDSWEPT_SAVANNA;
        return Biome::PLAINS;
    }
    if (c.temperature < 0.65f) {
        if (c.humidity > 0.75f) return Biome::DARK_FOREST;
        if (c.humidity > 0.55f) return Biome::BIRCH_FOREST;
        if (c.humidity > 0.4f)  return Biome::FOREST;
        return Biome::PLAINS;
    }
    if (c.temperature < 0.8f) {
        if (c.humidity > 0.7f) return Biome::SWAMP;
        if (c.humidity > 0.5f) return Biome::JUNGLE;
        if (c.humidity > 0.3f) return Biome::SAVANNA;
        return Biome::PLAINS;
    }
    if (c.humidity > 0.75f) return Biome::JUNGLE;
    if (c.humidity > 0.5f)  return Biome::BAMBOO_JUNGLE;
    if (c.humidity > 0.3f)  return Biome::SAVANNA_PLATEAU;
    if (c.erosion < 0.4f)   return Biome::ERODED_BADLANDS;
    return Biome::DESERT;
}

// ============================================================
// Section 5. 全局状态
// ============================================================
namespace G {

struct ChunkData {
    vector<uint8_t> blocks;
    vector<uint8_t> biomes;
    vector<uint8_t> lightMap; // ★ 加上这一行
    vector<uint16_t> colTop;              // 每列最高非空方块(相对RY0)，网格/光照快速路径
    atomic<bool> lightDirty{ false }; // 光照需要重算
    atomic<bool> dirty{ false };
    uint8_t lod = 0;     // ★ 0=实加载 1=地表桩(LOD)
    bool edited = false; // ★ 玩家改过(未改的桩不落盘)


    ChunkData() : blocks(CHUNK_BLOCK_COUNT, (uint8_t)Block::AIR), biomes(CHUNK_BIOME_COUNT, (uint8_t)Biome::PLAINS), lightMap(CHUNK_BLOCK_COUNT, 0), colTop(CHUNK_BIOME_COUNT, 0) {}

};

struct LightNode {
    int x, y, z; // 世界坐标
    uint8_t light; // 光照强度
};
std::queue<LightNode> lightQueue;

unordered_map<uint64_t, shared_ptr<ChunkData>> chunks;
mutex chunksMutex;

uint64_t seed = 0;
Noise2D nTemp, nHum, nCont, nEro, nWeird, nDetail, nSpike, nEntry, nRiver;
Noise3D nCheese, nCheese2;
Noise3D nSpag,   nSpag2;
Noise3D nNoodle, nNoodle2;
Noise3D nAquifer, nLava, nRough;


XMFLOAT3 camPos = XMFLOAT3(0.5f, 200.0f, 0.5f);
float    yaw    = 0.78f;
float    pitch  = -0.85f;
bool     keys[256] = {};

bool rightMouseDown = false;
bool leftMouseDown  = false;
bool mouseCaptured  = false;
int  lastMouseX     = 0;
int  lastMouseY     = 0;

int      playerMode = 0;
XMFLOAT3 playerVel  = XMFLOAT3(0,0,0);
bool     onGround   = false;
bool     inWater    = false;

Block    heldBlock      = Block::STONE;
int      heldBlockIndex = 0;
}

inline uint64_t chunkKey(int cx, int cz) {
    return ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz;
}
inline int toChunk(int wx) { return wx >> 4; }
inline int localChunk(int wx) { return wx & 15; }

inline size_t chunkBlockIndex(int lx, int wy, int lz) {
    return ((size_t)lx * CHUNK_SIZE + lz) * RH + (wy - RY0);
}
inline size_t chunkBiomeIndex(int lx, int lz) {
    return (size_t)lx * CHUNK_SIZE + lz;
}

inline Block getBlock(int wx, int wy, int wz) {
    if (wy < RY0 || wy >= RY1) return Block::AIR;
    int cx = toChunk(wx), cz = toChunk(wz);
    shared_ptr<G::ChunkData> cd;
    {
        lock_guard<mutex> lk(G::chunksMutex);
        auto it = G::chunks.find(chunkKey(cx, cz));
        if (it == G::chunks.end()) return Block::AIR;
        cd = it->second;
    }
    int lx = localChunk(wx), lz = localChunk(wz);
    return (Block)cd->blocks[chunkBlockIndex(lx, wy, lz)];
}

inline void setBlock(int wx, int wy, int wz, Block b) {
    if (wy < RY0 || wy >= RY1) return;
    int cx = toChunk(wx), cz = toChunk(wz);
    shared_ptr<G::ChunkData> cd;
    {
        lock_guard<mutex> lk(G::chunksMutex);
        auto it = G::chunks.find(chunkKey(cx, cz));
        if (it == G::chunks.end()) return;
        cd = it->second;
    }
    int lx = localChunk(wx), lz = localChunk(wz);
    cd->blocks[chunkBlockIndex(lx, wy, lz)] = (uint8_t)b;
    cd->dirty = true;
    cd->edited = true;   // ★ 玩家动过(保证落盘)
    if (b != Block::AIR) {

        uint16_t& ct = cd->colTop[chunkBiomeIndex(lx, lz)];
        if ((int)(RY0 + ct) < wy) ct = (uint16_t)(wy - RY0);
    }
}


inline Biome getBiome(int wx, int wz) {
    int cx = toChunk(wx), cz = toChunk(wz);
    shared_ptr<G::ChunkData> cd;
    {
        lock_guard<mutex> lk(G::chunksMutex);
        auto it = G::chunks.find(chunkKey(cx, cz));
        if (it == G::chunks.end()) return Biome::PLAINS;
        cd = it->second;
    }
    int lx = localChunk(wx), lz = localChunk(wz);
    return (Biome)cd->biomes[chunkBiomeIndex(lx, lz)];
}

//int lx = localChunk(wx), lz = localChunk(wz);
//return (Biome)cd->biomes[chunkBiomeIndex(lx, lz)];
//}

// ===== GI 专用:每帧 chunk 快照(避免百万次加锁)=====



// ============================================================
// Section 6. 世界生成
// ============================================================
// ============================================================
// Section 6. 世界生成
// ============================================================
struct Tuning {
    int canyonCount;
    int canyonSegMin, canyonSegMax;
    float canyonSegLenMin, canyonSegLenMax, canyonTurnAmp;
    float canyonWidthMin, canyonWidthMax;
    int canyonYTopMin, canyonYTopMax, canyonYBotMin, canyonYBotMax;
    int tubeCount, tubeLenMin, tubeLenMax;
    float tubeTurnYaw, tubeTurnPitch;
    int tubeRMin, tubeRMax;
    float tubeBulgeMin, tubeBulgeMax;
    float cheeseFreq;
    float cheeseThreshBase;
    float cheeseThreshDeep;
    float spagWidthShallow;
    float spagWidthDeep;
    float noodleWidthShallow;
    float noodleWidthDeep;
    float spikeFreq1, spikeFreq2, spikeAmp1, spikeAmp2, spikeTh1, spikeTh2;
    float entryFreq, entryThresh;
};

static Tuning g_tuning;


inline uint32_t seed32(uint32_t tag) {
    return (uint32_t)(G::seed ^ ((uint64_t)tag * 0x9E3779B97F4A7C15ULL));
}
inline uint64_t chunkRngSeed(int cx, int cz) {
    return G::seed
         ^ ((uint64_t)cx * 0x9E3779B97F4A7C15ULL)
         ^ ((uint64_t)cz * 0xBF58476D1CE4E5B9ULL);
}

static void reseedAllNoise(uint64_t seed) {
    uint32_t s = (uint32_t)(seed ^ (seed >> 32));
    G::nTemp   .reseed(s ^ 0x1001u);
    G::nHum    .reseed(s ^ 0x1002u);
    G::nCont   .reseed(s ^ 0x1003u);
    G::nEro    .reseed(s ^ 0x1004u);
    G::nWeird  .reseed(s ^ 0x1005u);
    G::nDetail .reseed(s ^ 0x1006u);
    G::nSpike  .reseed(s ^ 0x1007u);
    G::nEntry  .reseed(s ^ 0x1008u);
    G::nRiver  .reseed(s ^ 0x1009u);

    G::nCheese .reseed(s ^ 0x2001u);
    G::nCheese2.reseed(s ^ 0x2101u);
    G::nSpag   .reseed(s ^ 0x2002u);
    G::nSpag2  .reseed(s ^ 0x2102u);
    G::nNoodle .reseed(s ^ 0x2003u);
    G::nNoodle2.reseed(s ^ 0x2103u);
    G::nAquifer.reseed(s ^ 0x3001u);
    G::nLava   .reseed(s ^ 0x3002u);
    G::nRough .reseed(s ^ 0x3003u);

}

static void rollTuning(uint64_t seed) {
    Rng rng(seed ^ 0xA1B2C3D4E5F60718ULL);
    Tuning& t = g_tuning;

    int roll = rng.range(0, 99);
    if      (roll < 12) t.canyonCount = 0;
    else if (roll < 30) t.canyonCount = rng.range(1, 2);
    else if (roll < 62) t.canyonCount = rng.range(3, 5);
    else if (roll < 85) t.canyonCount = rng.range(6, 8);
    else                t.canyonCount = rng.range(9, 12);

    t.canyonSegMin = rng.range(3, 7);
    t.canyonSegMax = rng.range(8, 20);
    if (t.canyonSegMax <= t.canyonSegMin) t.canyonSegMax = t.canyonSegMin + 1;

    t.canyonSegLenMin = (float)rng.range(15, 30);
    t.canyonSegLenMax = (float)rng.range(40, 75);
    if (t.canyonSegLenMax <= t.canyonSegLenMin) t.canyonSegLenMax = t.canyonSegLenMin + 5.0f;

    t.canyonTurnAmp = 0.25f + rng.f01() * 0.65f;

    t.canyonWidthMin = (float)rng.range(3, 5);
    t.canyonWidthMax = (float)rng.range(6, 10);
    if (t.canyonWidthMax <= t.canyonWidthMin) t.canyonWidthMax = t.canyonWidthMin + 1.0f;

    t.canyonYTopMin = rng.range(80, 95);
    t.canyonYTopMax = rng.range(105, 125);
    if (t.canyonYTopMax <= t.canyonYTopMin) t.canyonYTopMax = t.canyonYTopMin + 5;

    t.canyonYBotMin = rng.range(4, 15);
    t.canyonYBotMax = rng.range(18, 35);
    if (t.canyonYBotMax <= t.canyonYBotMin) t.canyonYBotMax = t.canyonYBotMin + 3;

    t.tubeCount = rng.range(150, 350);
    t.tubeLenMin = rng.range(50, 100);
    t.tubeLenMax = rng.range(160, 320);
    if (t.tubeLenMax <= t.tubeLenMin) t.tubeLenMax = t.tubeLenMin + 40;
    t.tubeTurnYaw   = 0.12f + rng.f01() * 0.35f;
    t.tubeTurnPitch = 0.05f + rng.f01() * 0.15f;
    t.tubeRMin = 1;
    t.tubeRMax = rng.range(2, 4);
    if (t.tubeRMax <= t.tubeRMin) t.tubeRMax = t.tubeRMin + 1;
    t.tubeBulgeMin = 0.20f + rng.f01() * 0.20f;
    t.tubeBulgeMax = 0.80f + rng.f01() * 0.20f;
    if (t.tubeBulgeMax <= t.tubeBulgeMin) t.tubeBulgeMax = t.tubeBulgeMin + 0.1f;

    t.cheeseFreq = 0.007f + rng.f01() * 0.004f;
    t.cheeseThreshBase = 0.20f + rng.f01() * 0.06f;
    t.cheeseThreshDeep = 0.28f + rng.f01() * 0.06f;

    t.spagWidthShallow = 0.003f + rng.f01() * 0.003f;
    t.spagWidthDeep    = 0.010f + rng.f01() * 0.008f;
    t.noodleWidthShallow = 0.0015f + rng.f01() * 0.0015f;
    t.noodleWidthDeep    = 0.004f  + rng.f01() * 0.004f;

    t.spikeFreq1 = 0.0050f + rng.f01() * 0.0050f;
    t.spikeFreq2 = 0.0150f + rng.f01() * 0.0100f;
    t.spikeAmp1  = 30.0f  + rng.f01() * 25.0f;
    t.spikeAmp2  = 10.0f  + rng.f01() *  8.0f;
    t.spikeTh1   = 0.60f  + rng.f01() * 0.15f;
    t.spikeTh2   = 0.62f  + rng.f01() * 0.16f;

    t.entryFreq = 0.012f + rng.f01() * 0.008f;
    t.entryThresh = 0.72f + rng.f01() * 0.08f;

    // ===== 应用 Mod 地形覆盖 =====
    if (g_modSettings.loaded) {
        // 修改整体地形幅度
        t.spikeAmp1 *= g_modSettings.noiseAmplitude;
        t.spikeAmp2 *= g_modSettings.noiseAmplitude;
        
        // 如果开启了平坦模式
        if (g_modSettings.flatMode) {
            t.canyonCount = 0; // 没峡谷
            t.tubeCount = 0;   // 没矿洞
        }
    }
}




// ===== 峡谷(1.21 ewo.java 忠实版):原点chunk概率触发 + 逐步行走 + 每步抖动 =====
// ===== 峡谷(1.21 ewo.java 忠实版):原点chunk概率触发 + 逐步行走 + 每步抖动 =====
static const int CANYON_WORLD_HALF = 8192;   // ← 加这行(tube 原点撒放范围,generateTubes 仍需要)
//struct RavinePt { float x, y, z, hr, vr, w; };

//struct RavinePt { float x, y, z, hr, vr, w; };
struct RavinePt { float x, y, z, hr, vr, w, wl; };   // wl<RY0 表示不灌水

static const float RAV_SQRT6 = 2.4494897f;      // 截面 dy²/6 项
static const int   RAV_LAVA_Y = RY0 + 8;         // vanilla lava_level: above_bottom 8
static const float RAV_PROB = 0.02f;           // 每 chunk 触发概率(canyon.json)
static const float RAV_YSCALE = 3.0f;   // ★ 2.0 → 3.0,对齐 vertical_radius_center_factor
static const float RAV_WIDTH = 1.3f;   // ★ 新增:宽度总补偿旋钮(嫌宽调 1.15,嫌窄调 1.5)


static void buildRavine(std::vector<RavinePt>& pts, Rng& rng, int originX, int originZ) {
    int   steps = (int)(112.0f * (0.75f + rng.f01() * 0.25f));  // distance_factor
    float thick = 0.75f + rng.f01() * 1.15f;                    // thickness uniform(0.75,1.9)
    float yScale = RAV_YSCALE * (0.8f + rng.f01() * 0.4f);
    // ★★★ 就插在这里(宽度档案 wprof 之前)★★★
    float wl = -9999.0f;
    if (rng.f01() < 0.25f)                               // 25% 的峡谷整条蓄水
        wl = (float)SEA_LEVEL - 10.0f - rng.f01() * 30.0f;   // 液面 y ∈ [22, 52]
    // ★★★ 插入结束,下面是原有的 wprof 档案 ★★★
    std::vector<float> wprof((size_t)steps);

    float base = 1.0f + rng.f01() * rng.f01();
    for (int i = 0; i < steps; i++) {
        if (i == 0 || rng.range(0, 2) == 0) base = 1.0f + rng.f01() * rng.f01();
        wprof[(size_t)i] = base * base;
    }
    float x = (float)originX + rng.f01() * 16.0f;
    float z = (float)originZ + rng.f01() * 16.0f;
    float y = 15.0f + rng.f01() * 55.0f;          // 适配本世界高度:低地露天、高原为地下峡谷
    float yaw = rng.f01() * 6.2831853f;
    float pitch = (rng.f01() - 0.5f) * 0.25f;     // vertical_rotation ±0.125
    float dp = 0, din = 0;
    for (int s = 0; s < steps; s++) {
        float u = (float)s / (float)steps;
        float hrBase = 1.5f + sinf(u * 3.14159265f) * thick;   // sin 包络
        float taper = 1.0f - 2.0f * cosf(0.5f - u);
        float vr = fabsf(taper) * hrBase * RAV_YSCALE;      // ★ 替换原 |1.5+2.5*taper| 式
        vr *= 0.85f + 0.15f * rng.f01();                    // ★ 抖动下限 0.75→0.85(少压一层)
        float hr = hrBase * RAV_WIDTH * (0.85f + 0.15f * rng.f01());  // ★ 同上 + 宽度补偿

        pts.push_back({ x, y, z, hr, vr, wprof[(size_t)s], wl });
        float cp = cosf(pitch);
        x += cosf(yaw) * cp;  y += sinf(pitch);  z += sinf(yaw) * cp;
        pitch *= 0.7f;  pitch += dp * 0.05f;
        yaw += din * 0.05f;
        dp *= 0.8f;  din *= 0.5f;
        float a1 = rng.f01(), a2 = rng.f01(), a3 = rng.f01();
        dp += (a1 - a2) * a3 * 2.0f;
        din += (a2 - a1) * a3 * 4.0f;
    }
}

static void carveRavines(G::ChunkData& cd, int cx, int cz) {
    if (g_modSettings.loaded && g_modSettings.flatMode) return;
    int wx0 = cx * CHUNK_SIZE, wz0 = cz * CHUNK_SIZE;
    const int RAD = 8;
    std::vector<RavinePt> pts;
    for (int oz = -RAD; oz <= RAD; oz++)
        for (int ox = -RAD; ox <= RAD; ox++) {
            int ocx = cx + ox, ocz = cz + oz;
            Rng rng(chunkRngSeed(ocx, ocz) ^ 0x5AF1A5EULL);
            if (rng.f01() >= RAV_PROB) continue;
            pts.clear();
            buildRavine(pts, rng, ocx * CHUNK_SIZE, ocz * CHUNK_SIZE);
            for (auto& p : pts) {
                if (p.x + p.hr + 1 < (float)wx0 || p.x - p.hr - 1 >= (float)(wx0 + CHUNK_SIZE)) continue;
                if (p.z + p.hr + 1 < (float)wz0 || p.z - p.hr - 1 >= (float)(wz0 + CHUNK_SIZE)) continue;
                int yLo = max((int)floorf(p.y - p.vr * RAV_SQRT6), RY0 + 1);
                int yHi = min((int)ceilf(p.y + p.vr * RAV_SQRT6), RY1 - 2);
                for (int wy = yLo; wy <= yHi; wy++) {
                    float dyn = ((float)wy + 0.5f - p.y) / p.vr;
                    if (dyn * dyn >= 6.0f) continue;
                    float r2 = p.hr * p.hr * (1.0f - dyn * dyn / 6.0f) / p.w;
                    if (r2 <= 0.0f) continue;
                    float rr = sqrtf(r2);
                    int xLo = max((int)floorf(p.x - rr) - wx0, 0);
                    int xHi = min((int)ceilf(p.x + rr) - wx0, CHUNK_SIZE - 1);
                    int zLo = max((int)floorf(p.z - rr) - wz0, 0);
                    int zHi = min((int)ceilf(p.z + rr) - wz0, CHUNK_SIZE - 1);
                    for (int lx = xLo; lx <= xHi; lx++)
                        for (int lz = zLo; lz <= zHi; lz++) {
                            float fx = (float)(wx0 + lx) + 0.5f - p.x;
                            float fz = (float)(wz0 + lz) + 0.5f - p.z;
                            if (fx * fx + fz * fz >= r2) continue;
                            size_t idx = chunkBlockIndex(lx, wy, lz);
                            Block b = (Block)cd.blocks[idx];
                            if (b == Block::AIR || b == Block::WATER || b == Block::LAVA) continue;
                            uint8_t nb;
                            if (wy <= (int)p.wl)          nb = (uint8_t)Block::WATER;  // 蓄水峡谷:液面以下灌水
                            else if (wy <= RAV_LAVA_Y)    nb = (uint8_t)Block::LAVA;   // 深谷底:岩浆
                            else                          nb = (uint8_t)Block::AIR;    // 其余掏空
                            cd.blocks[idx] = nb;

                        }
                }
            }
        }
}

static bool inRavine(int x, int y, int z) {
    const int RAD = 8;
    int ocx0 = toChunk(x), ocz0 = toChunk(z);
    std::vector<RavinePt> pts;
    for (int oz = -RAD; oz <= RAD; oz++)
        for (int ox = -RAD; ox <= RAD; ox++) {
            int ocx = ocx0 + ox, ocz = ocz0 + oz;
            Rng rng(chunkRngSeed(ocx, ocz) ^ 0x5AF1A5EULL);
            if (rng.f01() >= RAV_PROB) continue;
            pts.clear();
            buildRavine(pts, rng, ocx * CHUNK_SIZE, ocz * CHUNK_SIZE);
            for (auto& p : pts) {
                float fx = (float)x + 0.5f - p.x, fz = (float)z + 0.5f - p.z;
                float dy = (float)y + 0.5f - p.y;
                if (fx * fx + fz * fz < p.hr * p.hr && fabsf(dy) < p.vr * RAV_SQRT6)
                    return true;
            }
        }
    return false;
}





struct CarverTube {
    vector<XMFLOAT3> points;
    vector<uint8_t>  radii;
    float minX, maxX, minY, maxY, minZ, maxZ;
};
static vector<CarverTube> g_tubes;

static void generateTubes(uint64_t seed) {
    g_tubes.clear();
    Rng rng(seed ^ 0xCA5E7B0ULL);
    Tuning& t = g_tuning;

    int total = 8000;
    for (int i = 0; i < total; i++) {
        CarverTube tb;
        float fx = (rng.f01() * 2.0f - 1.0f) * CANYON_WORLD_HALF;
        float fz = (rng.f01() * 2.0f - 1.0f) * CANYON_WORLD_HALF;
        float fy = (float)rng.range(RY0 + 8, 48);
        float yaw   = rng.f01() * 6.2831853f;
        float pitch = (rng.f01() - 0.5f) * 0.6f;

        int len  = rng.range(t.tubeLenMin, t.tubeLenMax);
        int rMax = rng.range(t.tubeRMin, t.tubeRMax);

        tb.minX = tb.maxX = fx;
        tb.minY = tb.maxY = fy;
        tb.minZ = tb.maxZ = fz;

        for (int step = 0; step < len; step++) {
            tb.points.push_back(XMFLOAT3(fx, fy, fz));
            float tt = (float)step / (float)len;
            float bulge = 1.0f - fabsf(tt * 2.0f - 1.0f);
            float rScaled = t.tubeBulgeMin + (t.tubeBulgeMax - t.tubeBulgeMin) * bulge;
            int r = max(t.tubeRMin, (int)(rMax * rScaled + 0.5f));
            tb.radii.push_back((uint8_t)r);

            float cp = cosf(pitch);
            float dx = cosf(yaw) * cp, dy = sinf(pitch), dz = sinf(yaw) * cp;

            yaw   += (rng.f01() - 0.5f) * 2.0f * t.tubeTurnYaw;
            pitch += (rng.f01() - 0.5f) * 2.0f * t.tubeTurnPitch;
            if (pitch >  0.7f) pitch =  0.7f;
            if (pitch < -0.7f) pitch = -0.7f;

            fx += dx; fy += dy; fz += dz;
            if (fy <= (float)(RY0 + 3)) { fy = (float)(RY0 + 3); pitch = fabsf(pitch) * 0.8f; }
            if (fy >= 52.0f)            { fy = 52.0f;            pitch = -fabsf(pitch) * 0.8f; }

            tb.minX = min(tb.minX, fx); tb.maxX = max(tb.maxX, fx);
            tb.minY = min(tb.minY, fy); tb.maxY = max(tb.maxY, fy);
            tb.minZ = min(tb.minZ, fz); tb.maxZ = max(tb.maxZ, fz);
        }
        g_tubes.push_back(std::move(tb));
    }
    printf("[GEN] %d tubes.\n", (int)g_tubes.size());
}


// ===== MC 原版噪声采样器:ImprovedPerlin(fkc) / PerlinNoise(fkf) / NormalNoise(fke) =====
struct McRandom {                      // MC 用 xoroshiro128+;RNG 只影响地貌指纹,不影响公式
    uint64_t s;
    explicit McRandom(uint64_t seed) : s(seed ? seed : 88172645463325252ULL) {}
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    double nextDouble() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }   // [0,1)
    int nextInt(int bound) { return (int)(next() % (uint64_t)bound); }
};

// fkc:ImprovedPerlin(单 octave,fkh.a 梯度表 + bgj.k fade)
struct McPerlin {
    double xo = 0, yo = 0, zo = 0;
    uint8_t p[256];
    void init(McRandom& r) {
        xo = r.nextDouble() * 256.0; yo = r.nextDouble() * 256.0; zo = r.nextDouble() * 256.0;
        for (int i = 0; i < 256; i++) p[i] = (uint8_t)i;
        for (int i = 0; i < 256; i++) {                                      // fkc 洗牌
            int j = r.nextInt(256 - i);
            uint8_t t = p[i]; p[i] = p[i + j]; p[i + j] = t;
        }
    }
    void init(uint64_t seed) { McRandom r(seed); init(r); }   // ★ 正确位置:struct 内、init(McRandom&) 之后

    static const int GRAD[16][3];
    static inline double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
    static inline double gdot(int h, double x, double y, double z) {
        const int* g = GRAD[h & 15];
        return (double)g[0] * x + (double)g[1] * y + (double)g[2] * z;
    }
    double sample(double x, double y, double z) const {
        x += xo; y += yo; z += zo;
        int X = (int)floor(x), Y = (int)floor(y), Z = (int)floor(z);
        double fx = x - X, fy = y - Y, fz = z - Z;
        int h0 = p[(X) & 255], h1 = p[(X + 1) & 255];
        int a = p[(h0 + Y) & 255], b = p[(h0 + 1 + Y) & 255];
        int c = p[(h1 + Y) & 255], d = p[(h1 + 1 + Y) & 255];
        double n000 = gdot(p[(a + Z) & 255], fx, fy, fz);
        double n100 = gdot(p[(a + 1 + Z) & 255], fx - 1, fy, fz);
        double n010 = gdot(p[(b + Z) & 255], fx, fy - 1, fz);
        double n110 = gdot(p[(b + 1 + Z) & 255], fx - 1, fy - 1, fz);
        double n001 = gdot(p[(c + Z) & 255], fx, fy, fz - 1);
        double n101 = gdot(p[(c + 1 + Z) & 255], fx - 1, fy, fz - 1);
        double n011 = gdot(p[(d + Z) & 255], fx, fy - 1, fz - 1);
        double n111 = gdot(p[(d + 1 + Z) & 255], fx - 1, fy - 1, fz - 1);
        double u = fade(fx), v = fade(fy), w = fade(fz);
        double x00 = n000 + (n100 - n000) * u, x10 = n010 + (n110 - n010) * u;
        double x01 = n001 + (n101 - n001) * u, x11 = n011 + (n111 - n011) * u;
        double y0 = x00 + (x10 - x00) * v, y1 = x01 + (x11 - x01) * v;
        return y0 + (y1 - y0) * w;
    }
    static inline double wrapCoord(double v) {
        return v - floor(v / 33554432.0 + 0.5) * 33554432.0;
    }
};

const int McPerlin::GRAD[16][3] = {
    {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},
    {0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1},{1,1,0},{0,-1,0},{0,1,0},{0,0,0}
};

// fkf:PerlinNoise(多 octave,amplitude 加权;非零 octave 各一个独立 perlin)
struct McOctaves {
    McPerlin oct[16];
    float amp[16];
    int firstOctave = 0, count = 0;
    double normE = 1.0, baseFreq = 1.0;
    void init(uint64_t seed, int first, const float* amplitudes, int n) {
        firstOctave = first; count = min(n, 16);
        memcpy(amp, amplitudes, sizeof(float) * count);
        McRandom r(seed);
        int nz = 0;
        for (int i = 0; i < count; i++) if (amp[i] != 0.0f) nz++;
        normE = pow(2.0, (double)(nz - 1)) / (pow(2.0, (double)nz) - 1.0);  // fkf: e = 2^(nz-1)/(2^nz-1)
        baseFreq = pow(2.0, (double)firstOctave);                           // fkf: f = 2^firstOctave
        for (int i = 0; i < count; i++) if (amp[i] != 0.0f) oct[i].init(r);
    }
    double sample(double x, double y, double z) const {
        double sum = 0.0, f = baseFreq, e = normE;
        for (int i = 0; i < count; i++) {
            if (amp[i] != 0.0f)
                sum += (double)amp[i] * oct[i].sample(McPerlin::wrapCoord(x * f), McPerlin::wrapCoord(y * f), McPerlin::wrapCoord(z * f)) * e;  // fkf.a
            f *= 2.0; e *= 0.5;
        }
        return sum;
    }
};

// fke:NormalNoise(两个 PerlinNoise,第二个坐标 ×1.0181268882175227,归一 0.1667/span)
struct McNoise {
    McOctaves a, b;
    double scale = 1.0;
    void init(uint64_t seed, int first, const float* amplitudes, int n) {
        a.init(seed * 2 + 1, first, amplitudes, n);
        b.init(seed * 2 + 2, first, amplitudes, n);
        int lo = 999, hi = -999;
        for (int i = 0; i < n; i++) if (amplitudes[i] != 0.0f) { lo = min(lo, i); hi = max(hi, i); }
        double span = (double)(hi - lo);
        double dev = 0.1 * (1.0 + 1.0 / (span + 1.0));                      // fke.a(int)
        scale = 0.16666666666666666 / dev;                                  // fke: c = 1/6/dev
    }
    double sample(double x, double y, double z) const {
        double s = 1.0181268882175227;                                      // fke: 第二 perlin 缩放
        return (a.sample(x, y, z) + b.sample(x * s, y * s, z * s)) * scale;
    }
};

// ===== MC 密度函数(常量出处:euw/eux/euk 反编译文件) =====
static McNoise mcCont, mcEro, mcRid, mcShiftX, mcShiftZ, mcTemp, mcHum;
static McPerlin mcJag;   // 近似 fkb BlendedNoise(0.25,0.125,80,160,8):主频 xz=1/320, y=1/1280

static Noise3D nAqBarrier, nAqFlood, nAqSpread, nAqLava;   // 含水层 4 噪声
static inline double aqN(const Noise3D& n, double x, double y, double z) {
    return (double)n.sample((float)x, (float)y, (float)z) * 2.0 - 1.0;  // [0,1]→[-1,1]
}


static void McNoiseInit(uint64_t seed) {
    static const float A_CONT[14] = { 1,1,1,0,1,1,0,0,0,0,0,1,1,1 };   // minecraft:continents(misode,待校验)
    static const float A_ERO[14] = { 1,1,0,1,1,0,0,0,0,0,0,1,0,1 };   // minecraft:erosion(misode,待校验)
    static const float A_RID[14] = { 1,2,1,0,1,1,0,0,0,0,0,1,1,1 };  // vanilla:ridges

    static const float A_SH[2] = { 1,1 };                            // minecraft:shift_x/z(misode,待校验)
    static const float A_T[11] = { 1,1,1,1,1,1,1,1,1,1,1 };          // minecraft:temperature(misode,待校验)
    mcCont.init(seed + 101, -9, A_CONT, 14);
    mcEro.init(seed + 102, -9, A_ERO, 14);
    mcRid.init(seed + 103, -7, A_RID, 14);
    mcShiftX.init(seed + 104, -3, A_SH, 2);
    mcShiftZ.init(seed + 105, -3, A_SH, 2);
    mcTemp.init(seed + 106, -10, A_T, 11);
    mcHum.init(seed + 107, -10, A_T, 11);
    mcJag.init(seed + 108);
    uint32_t s32 = (uint32_t)seed;
    nAqBarrier.reseed(s32 ^ 0x4001u);
    nAqFlood.reseed(s32 ^ 0x4002u);
    nAqSpread.reseed(s32 ^ 0x4003u);
    nAqLava.reseed(s32 ^ 0x4004u);


}

// shifted_noise(euk.w):pos×0.25 + shift(pos);shift(euk.v/t):noise((x,y,z)×0.25)×4
static inline float mcShifted(float x, float y, float z, const McNoise& n, const McNoise& sx, const McNoise& sz) {
    float shx = (float)(sx.sample(x * 0.25, y * 0.25, z * 0.25) * 4.0);
    float shz = (float)(sz.sample(x * 0.25, y * 0.25, z * 0.25) * 4.0);
    return (float)n.sample((x + shx) * 0.25, y, (z + shz) * 0.25);
}
// ridges_folded(eux.a(float) 原式):-(abs(|w|-2/3)-1/3)*3
static inline float mcPeaksValleys(float w) {
    return -(fabsf(fabsf(w) - 0.6666667f) - 0.33333334f) * 3.0f;
}
// depth(eux: y_clamped_gradient -64→320, 1.5→-1.5)
static inline float mcDepth(float y) {
    float t = (y + 64.0f) / 384.0f;
    if (t < 0) t = 0; if (t > 1) t = 1;
    return 1.5f - 3.0f * t;
}


// ================= MC 原版样条系统(rg.java 1:1 移植) =================
enum { S_C = 0, S_E = 1, S_W = 2, S_PV = 3 };   // 4 个输入坐标:大陆性/侵蚀/奇异/峰谷

struct Spl;
struct SNode { float loc, val, d; const Spl* child; };   // d=NaN 表示无导数
struct Spl { int in; std::vector<SNode> n; };

static float splEval(const Spl& s, const float v[4]) {
    const std::vector<SNode>& n = s.n;
    float x = v[s.in];
    auto val = [&](const SNode& a)->float { return a.child ? splEval(*a.child, v) : a.val; };
    if (x <= n.front().loc) return val(n.front());
    if (x >= n.back().loc)  return val(n.back());
    size_t i = 0; while (n[i + 1].loc < x) ++i;
    const SNode& a = n[i]; const SNode& b = n[i + 1];
    float t = (x - a.loc) / (b.loc - a.loc);
    if (a.d == a.d && b.d == b.d) {           // 两端都有导数 → 三次 Hermite(vanilla 同款)
        float t2 = t * t, t3 = t2 * t, h = b.loc - a.loc;
        return (2 * t3 - 3 * t2 + 1) * val(a) + (t3 - 2 * t2 + t) * h * a.d
            + (-2 * t3 + 3 * t2) * val(b) + (t3 - t2) * h * b.d;
    }
    return val(a) + (val(b) - val(a)) * t;    // 否则线性(vanilla 行为一致)
}

struct SB {                                    // 构建器
    int in; std::vector<SNode> n;
    explicit SB(int in_) : in(in_) {}
    SB& c(float loc, float v) { n.push_back({ loc, v, NAN, nullptr }); return *this; }
    SB& c(float loc, const Spl* s) { n.push_back({ loc, 0, NAN, s });       return *this; }
    SB& cd(float loc, float v, float d) { n.push_back({ loc, v, d,  nullptr });  return *this; }
    Spl* done() { return new Spl{ in, std::move(n) }; }
};

// ---- 脊状变换(vanilla 常数照搬) ----
static const float RK = 0.46082947f;
static float ridgeVal(float u, float target, float brk) {
    float s = 1.0f - (1.0f - target) * 0.5f, k = 0.5f * (1.0f - target);
    float r = (u + 1.17f) * RK * s - k;
    return u < brk ? (r < -0.2222f ? -0.2222f : r) : (r < 0.0f ? 0.0f : r);
}
static float ridgeInv(float target) {
    float s = 1.0f - (1.0f - target) * 0.5f, k = 0.5f * (1.0f - target);
    return k / (RK * s) - 1.17f;
}

// 带尖峰断点的脊样条(rg a(I,float,bool,bes),W 输入)
static Spl* peaksD(float target, bool amplified) {
    SB b(S_W);
    float v1 = ridgeVal(-1.0f, target, -0.7f);
    float v9 = ridgeVal(1.0f, target, -0.7f);
    float ub = ridgeInv(target);
    if (-0.65f < ub && ub < 1.0f) {
        float v75 = ridgeVal(-0.75f, target, -0.7f);
        float v65 = ridgeVal(-0.65f, target, -0.7f);
        float m1 = (v75 - v1) / (-0.75f + 1.0f);
        b.cd(-1.0f, v1, m1).c(-0.75f, v75).c(-0.65f, v65);
        float vb = ridgeVal(ub, target, -0.7f);
        float m2 = (v9 - vb) / (1.0f - ub);
        b.c(ub - 0.01f, vb).cd(ub, vb, m2).cd(1.0f, v9, m2);
    }
    else {
        float m = (v9 - v1) / 2.0f;
        if (amplified) b.cd(-1.0f, v1 < 0.2f ? 0.2f : v1, m).cd(0.0f, (v1 + v9) * 0.5f, m).cd(1.0f, v9, m);
        else           b.cd(-1.0f, v1, m).cd(1.0f, v9, m);
    }
    return b.done();
}

// 双节点脊(rg a(I,float,bes)):-0.01/+0.01 两侧不对称
static Spl* ridgeSpl2(int in, float target) {
    return SB(in).c(-0.01f, 0.63f * target).c(0.01f, 0.3f * target).done();
}

// rg 3 参 peaks:节点挂在 in1 上,脊挂在 in0 上
static Spl* peaksSpl(int in0, int in1, float t2, float t3) {
    SB b(in1);
    b.c(0.4f, 0.0f);
    if (t3 > 0.0f) b.c(0.48333333f, ridgeSpl2(in0, t3)); else b.c(0.48333333f, 0.0f);
    if (t2 > 0.0f) b.c(1.0f, ridgeSpl2(in0, t2)); else b.c(1.0f, 0.0f);
    return b.done();
}

// rg 6 参 Hermite 样条(导数按 vanilla 公式)
static Spl* herm7(int in, float f1, float f2, float f3, float f4, float f5, float f6) {
    float dLo = 0.5f * (f2 - f1); if (dLo < f6) dLo = f6;   // max(0.5*(f2-f1), f6)
    float dMd = 5.0f * (f3 - f2);
    return SB(in)
        .cd(-1.0f, f1, dLo)
        .cd(-0.4f, f2, dLo < dMd ? dLo : dMd)               // min(dLo, dMd)
        .cd(0.0f, f3, dMd)
        .cd(0.4f, f4, 2.0f * (f4 - f3))
        .cd(1.0f, f5, 0.7f * (f5 - f4))
        .done();
}


// rg 9 参:E→W 偏移子样条(offset 的 child)
static Spl* offsetSpl(float o2, float o3, float o4, float o5, float o6, float o7,
    bool peaks, bool islands) {
    const Spl* s14 = peaksD(o5 < 0.6f ? 0.6f : (o5 > 1.5f ? 1.5f : o5), islands);
    const Spl* s15 = peaksD(o5 < 0.6f ? 0.6f : (o5 > 1.0f ? 1.0f : o5), islands);
    const Spl* s16 = peaksD(o5, islands);
    const Spl* s17 = herm7(S_W, o2 - 0.15f, 0.5f * o5, 0.5f * o5, 0.5f * o5, 0.6f * o5, 0.5f);
    const Spl* s18 = herm7(S_W, o2, o6 * o5, o3 * o5, 0.5f * o5, 0.6f * o5, 0.5f);
    const Spl* s19 = herm7(S_W, o2, o6, o6, o3, o4, 0.5f);
    const Spl* s22 = herm7(S_W, -0.02f, o7, o7, o3, o4, 0.0f);
    SB b(S_E);
    b.c(-0.85f, s14).c(-0.70f, s15).c(-0.40f, s16).c(-0.35f, s17)
        .c(-0.10f, s18).c(0.20f, s19);
    if (peaks) {
        const Spl* s21 = SB(S_W).c(-1.0f, o2).c(-0.4f, s19).c(0.0f, o4 + 0.07f).done();
        b.c(0.40f, s19).c(0.45f, s21).c(0.55f, s21).c(0.58f, s19);
    }
    b.c(0.70f, s22);
    return b.done();
}

// rg factor 嵌套:W 顶层,子节点挂 PV,支线挂 E
static Spl* factorSpl(float val, bool peaks) {
    const Spl* A = SB(S_PV).c(-0.2f, 6.3f).c(0.2f, val).done();
    const Spl* B1 = SB(S_PV).c(-0.05f, 6.3f).c(0.05f, 2.67f).done();
    const Spl* B2 = SB(S_PV).c(-0.05f, 2.67f).c(0.05f, 6.3f).done();
    SB b(S_W);
    b.c(-0.6f, A).c(-0.5f, B1).c(-0.35f, A).c(-0.25f, A).c(-0.1f, B2).c(0.03f, A);
    if (peaks) {
        const Spl* C1 = SB(S_PV).c(0.0f, val).c(0.1f, 0.625f).done();
        const Spl* C2 = SB(S_E).c(-0.9f, val).c(-0.69f, C1).done();
        b.c(0.35f, val).c(0.45f, C2).c(0.55f, C2).c(0.62f, val);
    }
    else {
        const Spl* D1 = SB(S_E).c(-0.7f, A).c(-0.15f, 1.37f).done();
        const Spl* D2 = SB(S_E).c(0.45f, A).c(0.70f, 1.56f).done();
        b.c(0.05f, D2).c(0.40f, D2).c(0.45f, D1).c(0.55f, D1).c(0.58f, val);
    }
    return b.done();
}

// rg jaggedness 嵌套:E 顶层,脊挂 W,节点挂 E
static Spl* jag8(float t3, float t4, float t5, float t6) {
    const Spl* P1 = peaksSpl(S_W, S_PV, t3, t5);
    const Spl* P2 = peaksSpl(S_W, S_PV, t4, t6);

    return SB(S_E).c(-1.0f, P1).c(-0.78f, P2).c(-0.5775f, P2).c(-0.375f, 0.0f).done();
}

// ---- 三条顶层样条(节点表逐点照搬 rg.java) ----
static const Spl* g_off = nullptr;
static const Spl* g_fac = nullptr;
static const Spl* g_jag = nullptr;

static void initTerrainSplines() {
    // offset:顶层 = continentalness
    const Spl* d1 = offsetSpl(-0.15f, 0.0f, 0.0f, 0.1f, 0.0f, -0.03f, false, false);
    const Spl* d2 = offsetSpl(-0.10f, 0.03f, 0.1f, 0.1f, 0.01f, -0.03f, false, false);
    const Spl* d3 = offsetSpl(-0.10f, 0.03f, 0.1f, 0.7f, 0.01f, -0.03f, true, true);
    const Spl* d4 = offsetSpl(-0.05f, 0.03f, 0.1f, 1.0f, 0.01f, 0.01f, true, true);
    g_off = SB(S_C)
        .c(-1.10f, 0.044f)      // 蘑菇岛
        .c(-1.02f, -0.2222f)     // 深海洋底(常数段)
        .c(-0.51f, -0.2222f)
        .c(-0.44f, -0.12f)
        .c(-0.18f, -0.12f)
        .c(-0.16f, d1).c(-0.15f, d1)
        .c(-0.10f, d2)
        .c(0.25f, d3)
        .c(1.00f, d4)
        .done();

    // factor:顶层 = erosion
    g_fac = SB(S_E)
        .c(-0.19f, 3.95f)
        .c(-0.15f, factorSpl(6.25f, true))
        .c(-0.10f, factorSpl(5.47f, true))
        .c(0.03f, factorSpl(5.08f, true))
        .c(0.06f, factorSpl(4.69f, false))
        .done();

    // jaggedness:顶层 = peaks&valleys
    g_jag = SB(S_PV)
        .c(-0.11f, 0.0f)
        .c(0.03f, jag8(1.0f, 0.5f, 0.0f, 0.0f))
        .c(0.65f, jag8(1.0f, 1.0f, 1.0f, 0.0f))
        .done();
}









// 最终密度:sloped_cheese × factor(wiki:"sloped cheese multiplied by factor";地表=sloped 过零,坡度=factor)
// 最终密度:sloped_cheese × factor(vanilla 公式)
//int mcSurfaceHeight(int x, int z);   // 前置声明,定义在原处不动

static float mcTerrainDensity(float x, float y, float z, float C, float E, float w) {
    float pv = mcPeaksValleys(w);
    float vOff[4] = { C, E, pv, pv };            // ★ offset 的 W 槽喂 pv(folded),不是 w
    float off = splEval(*g_off, vOff) - 0.50375f;
    float v[4] = { C, E, w, pv };              // factor/jagged 仍用原始 w + pv,不变
    float fac = splEval(*g_fac, v);
   // ...

    float jag = splEval(*g_jag, v)
        * fabsf((float)mcJag.sample(x * 0.003125f, y * 0.00078125f, z * 0.003125f)); // ★ abs,只向上刺
    float sloped = mcDepth(y) + off + jag;
    return sloped * fac;
}

// ================= Aquifer(euc.java 1:1 移植) =================
#include <cstdint>
#include <cfloat>

enum AqFluid { AQ_AIR = 0, AQ_WATER = 1, AQ_LAVA = 2 };
static const int AQ_SENTINEL = -1073741824;        // esh.g:无含水层
static int fdiv12(int a) { return a >= 0 ? a / 12 : -((-a + 11) / 12); }
static int fdivN(int a, int n) { return a >= 0 ? a / n : -((-a + n - 1) / n); }
static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
// bgj.a(v,lo,hi,outLo,outHi):钳制线性重映射(vanilla 5 参插值)
static double remapd(double v, double lo, double hi, double outLo, double outHi) {
    double t = clampd((v - lo) / (hi - lo), 0.0, 1.0);
    return outLo + (outHi - outLo) * t;
}

//extern uint64_t g_worldSeed;                        // 你已有(McNoiseInit 里存过,没有就补一个全局)

// ---- 位置随机(vanilla evd:节点 = 格子坐标 + 格内随机偏移) ----
static uint64_t aqHash(int x, int y, int z) {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ G::seed;
    h ^= (uint64_t)(uint32_t)x * 0xC2B2AE3D27D4EB4Full;
    h ^= (uint64_t)(uint32_t)y * 0x165667B19E3779F9ull;
    h ^= (uint64_t)(uint32_t)z * 0x27D4EB2F165667C5ull;
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33; h *= 0xC4CEB9FE1A85EC53ull; h ^= h >> 33;
    return h;
}
static int aqRand(int x, int y, int z, int bound) { return (int)(aqHash(x, y, z) % (unsigned)bound); }

// ---- 4 条噪声钩子:接你生成 mcJag 的同款 Perlin,各建独立实例 ----
// vanilla data/minecraft/worldgen/noise/*.json 参数(可在你 jar 里核对):
//   aquifer_barrier: firstOctave -3, [1,1,1,1]
//   aquifer_fluid_level_floodedness: -3, [1,1,1,1]
//   aquifer_fluid_level_spread:      -3, [1,1,1,1]
//   lava:                            -1, [1,1]
//struct AqNoise { double (*sample)(double, double, double); };
//static AqNoise nBarrier, nFlood, nSpread, nLava;    // ★ 启动时用你的 perlin 工厂初始化这 4 个

struct AquStatus {
    int level; AqFluid fluid;
    AqFluid at(int y) const { return y < level ? fluid : AQ_AIR; }   // record b.a(y)
    bool operator==(const AquStatus& o) const { return level == o.level && fluid == o.fluid; }
    bool operator!=(const AquStatus& o) const { return !(*this == o); }
};

static const int kOffsets[13][2] = {                 // O 表,原样照搬
    {0,0},{-2,-1},{-1,-1},{0,-1},{1,-1},{-3,0},{-2,0},{-1,0},{1,0},{-2,1},{-1,1},{0,1},{1,1}
};



//static float mcSurfaceHeight(float x, float z);   // 前置声明(真身在下方)


static float mcSurfaceHeight(float x, float z, float* outC = nullptr, float* outE = nullptr) {
    float C = mcShifted(x, 0, z, mcCont, mcShiftX, mcShiftZ);
    float E = mcShifted(x, 0, z, mcEro, mcShiftX, mcShiftZ);
    float w = mcShifted(x, 0, z, mcRid, mcShiftX, mcShiftZ);

    if (outC) *outC = C;
    if (outE) *outE = E;
    float prevY = 200.0f, prevD = mcTerrainDensity(x, prevY, z, C, E, w);
    for (float y = 196.0f; y >= -60.0f; y -= 4.0f) {
        float d = mcTerrainDensity(x, y, z, C, E, w);
        if (prevD <= 0.0f && d > 0.0f) {
            float a = y, b = y + 4.0f;
            for (int i = 0; i < 8; i++) {
                float m = 0.5f * (a + b);
                if (mcTerrainDensity(x, m, z, C, E, w) > 0.0f) b = m; else a = m;
            }
            return 0.5f * (a + b);
        }
        prevY = y; prevD = d;
    }
    return (float)SEA_LEVEL - 30.0f;   // 深海兜底
}


static bool aqLavaGate(int x, int y, int z) {
    (void)x; (void)y; (void)z;
    return false;   // TODO:dye.a(F,G,pos) 真身待补;false = 正常含水层行为(不抑制)
}



class Aquifer {
public:
    // $$1 = 列范围(e=minX,g=maxX,f=minZ,h=maxZ),$$4=minY,$$5=高度
    Aquifer(int minX, int maxX, int minZ, int maxZ, int minY, int height) {
        cJ = (minX - 5) >> 4;
        int lastX = ((maxX - 5) >> 4) + 1;
        cM = lastX - cJ + 1;
        cK = fdiv12(minY + 1) - 1;
        int lastY = fdiv12(minY + height + 1) + 1;
        cRows = lastY - cK + 1;
        cL = (minZ - 5) >> 4;
        int lastZ = ((maxZ - 5) >> 4) + 1;
        cN = lastZ - cL + 1;
        int total = cM * cRows * cN;
        haveNode = new bool[total]();  haveStatus = new bool[total]();
        nodePos = new int[total * 3]; status = new AquStatus[total];
        // I 顶界:floorDiv(表面+8+12,12)*12 + 10(表面用区域中心一列概略)
        int surf = surfaceEstimate((cJ << 4) + 8, (cL << 4) + 8);
        I = fdiv12(surf + 20) * 12 + 10;
    }
    ~Aquifer() { delete[] haveNode; delete[] haveStatus; delete[] nodePos; delete[] status; }

    // 主体。约定:density<=0 时才调用;返回该体素流体(空气/水/岩浆)
    AqFluid computeSubstance(int x, int y, int z, double density) {
        if (density > 0.0) return AQ_AIR;                       // vanilla 返回 null → 调用方实心
        //AquStatus global = globalFluid();

        // 然后 setBlock(x, y, z, b)(用你原来写方块的那行)


        // vanilla 返回 null → 调用方实心
        AquStatus global = globalFluid();
        if (y > I) return global.at(y);                         // 含水层区之上:只有海
        if (global.at(y) == AQ_LAVA) return AQ_LAVA;            // 自定义全局流体为岩浆时
        int cx = (x - 5) >> 4, cy = fdiv12(y + 1), cz = (z - 5) >> 4;
        int bi[4] = { 0,0,0,0 }; double bd[4] = { DBL_MAX,DBL_MAX,DBL_MAX,DBL_MAX };
        for (int ox = 0; ox <= 1; ++ox)                         // 与 vanilla 相同的三层嵌套顺序
            for (int oy = -1; oy <= 1; ++oy)
                for (int oz = 0; oz <= 1; ++oz) {
                    int gx = cx + ox, gy = cy + oy, gz = cz + oz;
                    int id = idx(gx, gy, gz);
                    if (!haveNode[id]) {                                // 节点 = 格子 + 随机偏移(桶 10/9/10)
                        nodePos[id * 3 + 0] = (gx << 4) + aqRand(gx, gy, gz, 10);
                        nodePos[id * 3 + 1] = gy * 12 + aqRand(gy, gx, gz, 9);
                        nodePos[id * 3 + 2] = (gz << 4) + aqRand(gz, gy, gx, 10);
                        haveNode[id] = true;
                    }
                    double dx = nodePos[id * 3 + 0] - x, dy = nodePos[id * 3 + 1] - y, dz = nodePos[id * 3 + 2] - z;
                    double dist = dx * dx + dy * dy + dz * dz;
                    if (bd[0] >= dist) { bd[3] = bd[2]; bi[3] = bi[2]; bd[2] = bd[1]; bi[2] = bi[1]; bd[1] = bd[0]; bi[1] = bi[0]; bd[0] = dist; bi[0] = id; }
                    else if (bd[1] >= dist) { bd[3] = bd[2]; bi[3] = bi[2]; bd[2] = bd[1]; bi[2] = bi[1]; bd[1] = dist; bi[1] = id; }
                    else if (bd[2] >= dist) { bd[3] = bd[2]; bi[3] = bi[2]; bd[2] = dist; bi[2] = id; }
                    else if (bd[3] >= dist) { bd[3] = dist; bi[3] = id; }
                }
        AquStatus s1 = statusOf(bi[0]);
        double sim01 = sim(bd[0], bd[1]);
        AqFluid cur = s1.at(y);
        if (sim01 <= 0.0) return cur;                           // 两节点同液面:直接给流体
        if (cur == AQ_WATER && globalFluidAt(y - 1).at(y - 1) == AQ_LAVA) return cur;
        double bCache = DBL_MAX;                                // MutableDouble 缓存
        AquStatus s2 = statusOf(bi[1]);
        double bar1 = sim01 * barrierStrength(x, y, z, s1, s2, bCache);
        if (density + bar1 > 0.0) return AQ_AIR;                // barrier 顶住:保持空气
        double sim02 = sim(bd[0], bd[2]);
        if (sim02 > 0.0) {
            double bar2 = sim01 * sim02 * barrierStrength(x, y, z, s1, statusOf(bi[2]), bCache);
            if (density + bar2 > 0.0) return AQ_AIR;
        }
        double sim12 = sim(bd[1], bd[2]);
        if (sim12 > 0.0) {
            double bar3 = sim01 * sim12 * barrierStrength(x, y, z, s2, statusOf(bi[2]), bCache);
            if (density + bar3 > 0.0) return AQ_AIR;
        }
        return cur;
    }

private:
    static double sim(double a, double b) { return 1.0 - (b - a) / 25.0; }   // a<=b,平方距离

    static AquStatus globalFluid() { return { 63, AQ_WATER }; }        // 海平面全局流体
    static AquStatus globalFluidAt(int y) { return y < 63 ? AquStatus{ 63, AQ_WATER } : AquStatus{ 63, AQ_AIR }; }

    int idx(int gx, int gy, int gz) const { return ((gy - cK) * cN + (gz - cL)) * cM + (gx - cJ); }

    AquStatus statusOf(int id) {
        if (haveStatus[id]) return status[id];
        AquStatus s = computeStatusAt(nodePos[id * 3 + 0], nodePos[id * 3 + 1], nodePos[id * 3 + 2]);
        status[id] = s; haveStatus[id] = true;
        return s;
    }

    AquStatus computeStatusAt(int nx, int ny, int nz) {
        AquStatus global = globalFluid();
        int minSurf = INT32_MAX, up = ny + 12, dn = ny - 12;
        bool nearSurfWater = false;
        for (int i = 0; i < 13; ++i) {
            int sx = nx + kOffsets[i][0], sz = nz + kOffsets[i][1];
            int surf = surfaceEstimate(sx, sz);                 // w.a(x,z) → 接 mcSurfaceHeight
            int surf8 = surf + 8;                               // e()
            bool center = (kOffsets[i][0] == 0 && kOffsets[i][1] == 0);
            if (center && dn > surf8) return global;            // 深埋:直接全局流体
            bool above = up > surf8;
            if (above || center) {
                AquStatus s2 = globalFluidAt(surf8);
                if (s2.at(surf8) != AQ_AIR) {                   // 海底列:地表+8 处仍是水
                    if (above) return s2;                       // 近海节点:直接海水
                    nearSurfWater = true;
                }
            }
            if (surf < minSurf) minSurf = surf;
        }
        int level = calcLevel(nx, ny, nz, minSurf, nearSurfWater);
        return { level, lavaOrWater(nx, ny, nz, level) };
    }

    int calcLevel(int x, int y, int z, int minSurf, bool bl) {
        double d, e;
        if (aqLavaGate(x, y, z)) { d = e = -1.0; }              // dye.a(F,G,...) 见文末缺口①
        else {
            int rel = minSurf + 8 - y;
            double t = bl ? remapd((double)rel, 0.0, 64.0, 1.0, 0.0) : 0.0;
            double flooded = clampd(aqN(nAqFlood, (double)x, (double)y, (double)z), -1.0, 1.0);
            double thE = remapd(t, 1.0, 0.0, -0.3, 0.8);        // $$13
            double thD = remapd(t, 1.0, 0.0, -0.8, 0.4);        // $$14
            d = flooded - thD;
            e = flooded - thE;
        }
        if (e > 0.0) return 63;                                 // 浅层含水层齐海平面
        if (d > 0.0) return spreadLevel(x, y, z, minSurf);      // 深层:spread 决定液面
        return AQ_SENTINEL;                                     // 此地无含水层(洞穴干燥)
    }

    int spreadLevel(int x, int y, int z, int minSurf) {
        int cy = fdivN(y, 40);
        int base = cy * 40 + 20;
        double s = aqN(nAqSpread, (double)fdivN(x, 16), (double)cy, (double)fdivN(z, 16)) * 10.0;

        int lvl = base + clampd(s, -3.0, 3.0);                  // bgj.a(s,3):±3 对称钳制
        return lvl < minSurf ? lvl : minSurf;
    }

    AqFluid lavaOrWater(int x, int y, int z, int level) {
        AqFluid f = AQ_WATER;
        if (level <= -10 && level != AQ_SENTINEL) {
            double v = aqN(nAqLava, (double)fdivN(x, 64), (double)fdivN(y, 40), (double)fdivN(z, 64));

            if (v > 0.3 || v < -0.3) f = AQ_LAVA;               // |lava| > 0.3 → 岩浆层
        }
        return f;
    }

    double barrierStrength(int x, int y, int z, AquStatus s1, AquStatus s2, double& cache) {
        AqFluid f1 = s1.at(y), f2 = s2.at(y);
        if ((f1 == AQ_LAVA && f2 == AQ_WATER) || (f1 == AQ_WATER && f2 == AQ_LAVA))
            return 2.0;                                          // 水/岩浆交界:恒成墙
        int diff = s1.level - s2.level; if (diff < 0) diff = -diff;
        if (diff == 0) return 0.0;
        double mid = 0.5 * (s1.level + s2.level);
        double dy = y + 0.5 - mid;
        double gap = diff / 2.0 - (dy < 0 ? -dy : dy);
        double q;
        if (dy > 0.0) { q = (gap > 0.0) ? gap / 1.5 : gap / 2.5; }
        else { double t = 3.0 + gap; q = (t > 0.0) ? t / 3.0 : t / 10.0; }
        double bar = 0.0;
        if (q >= -2.0 && q <= 2.0) {                            // $$19 ∈ [-2,2] 才吃 barrier 噪声
            if (cache == DBL_MAX) cache = aqN(nAqBarrier, (double)x, (double)y, (double)z);
            bar = cache;
        }
        return 2.0 * (bar + q);
    }

    static int surfaceEstimate(int x, int z) { return mcSurfaceHeight(x, z); }  // ★ 接你的地表估计

    int cJ, cM, cK, cRows, cL, cN, I;
    bool* haveNode; bool* haveStatus; int* nodePos; AquStatus* status;
};



// 地表高度:粗扫 4 格 + 二分(列级 C/E/w 只算一次)



// ============ 统一地形模型：高度/群系同源，调参只动这个面板 ============
static inline float tmSat(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float tmLerp(float a, float b, float t) { return a + (b - a) * t; }

struct TerrainOut { float h; Climate c; Biome b; };

static TerrainOut terrainModel(float x, float z) {
    // ---- 调参面板 ----
    const float CONT_F = 0.0016f;  const int CONT_O = 6;   // 大陆性 ~600格
    const float ERO_F = 0.0021f;  const int ERO_O = 4;   // 侵蚀 ~480格
    const float WEI_F = 0.0035f;  const int WEI_O = 3;   // 奇异 ~290格
    const float TEMP_F = 0.0008f, HUM_F = 0.0011f;       // 温/湿 1km+
    const float WARP = 24.0f;                            // 域扭曲
    const float MESO_F = 0.020f;                           // 中尺度丘陵 ~50格
    const float JAG_F = 0.035f, JAG_A = 26.0f;           // 尖峰
    const float PEAK_T = 0.52f, PEAK_W = 0.30f;          // 山门槛/过渡
    const float OCEAN_D = 0.10f, SHELF_D = 0.22f;         // 深海/大陆架
    const float RIVER_CARVE = 6.0f;                        // 河床下切

    float wx = x, wz = z;
    G::nWeird.domainWarp(wx, wz, WARP);
    Climate c;
    c.temperature = widen01(G::nTemp.fbm(wx * TEMP_F, wz * TEMP_F, 3), 2.6f);
    c.humidity = widen01(G::nHum.fbm(wx * HUM_F + 100.0f, wz * HUM_F + 50.0f, 3), 2.6f);
    c.continentalness = widen01(G::nCont.fbm(wx * CONT_F + 200.0f, wz * CONT_F + 150.0f, CONT_O), 2.4f);
    c.erosion = widen01(G::nEro.fbm(wx * ERO_F, wz * ERO_F, ERO_O), 2.4f);
    c.weirdness = widen01(G::nWeird.fbm(x * WEI_F, z * WEI_F, WEI_O), 2.2f);
    Biome b = selectBiome(c);
    BiomeProfile p = biomeProfile(b);
    float cc = c.continentalness;

    // offset：大陆性 → 基准海拔(相对 SEA+4)
    float off;
    if (cc < OCEAN_D)  off = tmLerp(-26.0f, -8.0f, cc / OCEAN_D);
    else if (cc < SHELF_D)  off = tmLerp(-8.0f, 0.0f, (cc - OCEAN_D) / (SHELF_D - OCEAN_D));
    else if (cc < 0.40f)    off = tmLerp(0.0f, 8.0f, (cc - SHELF_D) / 0.18f);
    else if (cc < 0.65f)    off = tmLerp(8.0f, 22.0f, (cc - 0.40f) / 0.25f);
    else                    off = tmLerp(22.0f, 32.0f, tmSat((cc - 0.65f) / 0.35f));

    // factor：侵蚀 → 起伏幅度(高侵蚀=平，低侵蚀=崎岖)
    float factor = tmLerp(20.0f, 3.0f, tmSat(c.erosion * 1.4f));
    factor *= tmLerp(0.7f, 1.5f, tmSat((cc + 0.2f) / 1.2f));

    // 山：weirdness 三角折叠 + 侵蚀门控
    float pk = 1.0f - fabsf(2.0f * fabsf(c.weirdness - 0.6667f) - 1.0f);
    float mMask = tmSat((c.weirdness - PEAK_T) / PEAK_W) * (1.0f - tmSat(c.erosion * 1.2f));
    if (p.mountain) mMask = max(mMask, 0.55f);

    float meso = G::nDetail.fbm(x * MESO_F, z * MESO_F, 4) - 0.5f;
    float spike = G::nSpike.ridged(x * JAG_F, z * JAG_F, 4);
    float h = (float)(SEA_LEVEL + 4) + off + meso * factor + pk * 14.0f * mMask
        + spike * spike * JAG_A * mMask;

    if (p.ocean && h > (float)(SEA_LEVEL - 2)) h = (float)(SEA_LEVEL - 2);
    // ★ 河流平滑化:连续掩码代替群系二值下切
    float riverBand = tmSat((c.weirdness - 0.86f) / 0.08f)          // 0.86→0.94 渐入河带
        * tmSat((0.42f - c.continentalness) / 0.15f);   // 只在低地成河,高原不切
    float riverWobble = G::nDetail.sample(x * 0.012f, z * 0.012f);  // 河道宽窄深浅起伏
    h -= RIVER_CARVE * (0.4f + riverWobble) * riverBand;            // 深 2~9 格渐变


    bool allowMinH = (!g_modSettings.loaded || !g_modSettings.noHeightLimit);
    if (allowMinH && !p.ocean && b != Biome::RIVER && b != Biome::FROZEN_RIVER
        && b != Biome::BEACH && b != Biome::SNOWY_BEACH && b != Biome::STONY_SHORE) {
        float minH = (float)(SEA_LEVEL + 2);
        if (h < minH) { float d2 = minH - h; h += d2 * tmSat(d2 / 18.0f); }
    }
    return { h, c, b };
}


static void computeChunkHeight(int cx, int cz, float* out) {
    const int EXT = 4;
    const int W = CHUNK_SIZE + 2 * EXT;

    for (int ez = 0; ez < W; ez++) for (int ex = 0; ex < W; ex++) {
        int wx = cx * CHUNK_SIZE - EXT + ex;
        int wz = cz * CHUNK_SIZE - EXT + ez;

        float wxF = (float)wx, wzF = (float)wz;
        out[ez * W + ex] = mcSurfaceHeight((float)wx, (float)wz);

    
    }

    static thread_local vector<float> tmp;
    tmp.assign(W * W, 0.0f);
    for (int iter = 0; iter < 4; iter++) {
        for (int z = 0; z < W; z++) for (int x = 0; x < W; x++) {
            float sum = 0; int wsum = 0;
            for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++) {
                int qx = x + dx, qz = z + dz;
                if (qx < 0 || qx >= W || qz < 0 || qz >= W) continue;
                int w = 5 - (abs(dx) + abs(dz));
                if (w <= 0) continue;
                sum += out[qz * W + qx] * (float)w;
                wsum += w;
            }
            tmp[z * W + x] = sum / (float)wsum;
        }
        memcpy(out, tmp.data(), sizeof(float) * W * W);
    }
}


// ============================================================
// 核心：体素光照 BFS 传播（类似 MC Java 版）
// ============================================================
// ============================================================
// 体素光照 BFS（改良版）
// 语义：lightMap 只记录透明格里的光；不透明格恒 0。
//       面亮度在建模时读相邻透明格，崖壁/洞壁不再莫名发黑。
// ============================================================
static void CalculateLightBFS(G::ChunkData& cd, int cx, int cz) {
    (void)cx; (void)cz;
    std::fill(cd.lightMap.begin(), cd.lightMap.end(), (uint8_t)0);

    bool haveTop = (cd.colTop.size() == (size_t)CHUNK_AREA);

    // ---------- 1. 天光：有列顶表时，列顶以上直接填满，只往下扫到地表 ----------
    int colTop[CHUNK_SIZE][CHUNK_SIZE];
    int maxTop = RY0 - 1;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int ct;
            if (haveTop) {
                ct = RY0 + (int)cd.colTop[chunkBiomeIndex(lx, lz)];
            }
            else {
                ct = RY0 - 1;
                for (int y = RY1 - 1; y >= RY0; y--)
                    if (cd.blocks[chunkBlockIndex(lx, y, lz)] != (uint8_t)Block::AIR) { ct = y; break; }
            }
            for (int y = RY1 - 1; y > ct; y--)
                cd.lightMap[chunkBlockIndex(lx, y, lz)] = 15;
            int sky = 15;
            int top = RY0 - 1;
            for (int y = ct; y >= RY0; y--) {
                size_t idx = chunkBlockIndex(lx, y, lz);
                Block b = (Block)cd.blocks[idx];
                if (!isTransparent(b)) { top = y; break; }
                if (b == Block::WATER) sky = std::max(0, sky - 2);
                cd.lightMap[idx] = (uint8_t)sky;
            }
            colTop[lx][lz] = top;
            if (top > maxTop) maxTop = top;
        }
    }

    std::queue<std::array<int, 4>> q;

    // ---------- 2. 岩浆光源(只扫列顶以下) ----------
    for (int lx = 0; lx < CHUNK_SIZE; lx++)
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int ct = haveTop ? (RY0 + (int)cd.colTop[chunkBiomeIndex(lx, lz)]) : (RY1 - 1);
            for (int y = RY0; y <= ct; y++) {
                size_t idx = chunkBlockIndex(lx, y, lz);
                if (cd.blocks[idx] == (uint8_t)Block::LAVA) {
                    cd.lightMap[idx] = 15;
                    q.push({ lx, y, lz, 15 });
                }
            }
        }

    const int dirs[6][3] = { {0,1,0},{0,-1,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1} };

    // ---------- 3. 只把能照亮更暗邻居的格子入队 ----------
    int seedCeil = std::min(RY1 - 1, std::max(maxTop, SEA_LEVEL) + 1);
    for (int lx = 0; lx < CHUNK_SIZE; lx++)
        for (int lz = 0; lz < CHUNK_SIZE; lz++)
            for (int y = RY0; y <= seedCeil; y++) {
                size_t idx = chunkBlockIndex(lx, y, lz);
                int light = cd.lightMap[idx];
                if (light <= 1) continue;
                if (!isTransparent((Block)cd.blocks[idx])) continue;
                for (int d = 0; d < 6; d++) {
                    int nx = lx + dirs[d][0], ny = y + dirs[d][1], nz = lz + dirs[d][2];
                    if (nx < 0 || nx >= CHUNK_SIZE || nz < 0 || nz >= CHUNK_SIZE) continue;
                    if (ny < RY0 || ny >= RY1) continue;
                    size_t nidx = chunkBlockIndex(nx, ny, nz);
                    Block nb = (Block)cd.blocks[nidx];
                    if (!isTransparent(nb)) continue;
                    int cost = 1 + (nb == Block::WATER ? 1 : 0);
                    if (light - cost > cd.lightMap[nidx]) { q.push({ lx, y, lz, light }); break; }
                }
            }

    // ---------- 4. BFS 传播 ----------
    while (!q.empty()) {
        auto node = q.front(); q.pop();
        int lx = node[0], y = node[1], lz = node[2], light = node[3];
        if (light != cd.lightMap[chunkBlockIndex(lx, y, lz)]) continue;
        for (int d = 0; d < 6; d++) {
            int nx = lx + dirs[d][0], ny = y + dirs[d][1], nz = lz + dirs[d][2];
            if (nx < 0 || nx >= CHUNK_SIZE || nz < 0 || nz >= CHUNK_SIZE) continue;
            if (ny < RY0 || ny >= RY1) continue;
            size_t nidx = chunkBlockIndex(nx, ny, nz);
            Block nb = (Block)cd.blocks[nidx];
            if (!isTransparent(nb)) continue;
            int newLight = light - 1 - (nb == Block::WATER ? 1 : 0);
            if (newLight > cd.lightMap[nidx]) {
                cd.lightMap[nidx] = (uint8_t)newLight;
                q.push({ nx, ny, nz, newLight });
            }
        }
    }
}


static void generateChunkData(G::ChunkData& cd, int cx, int cz, bool stub) {

    // ★ 矩阵模式拦截（原样保留）
    if (g_modSettings.loaded && g_modSettings.gridMode)
    {
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int wx = cx * CHUNK_SIZE + lx;
                int wz = cz * CHUNK_SIZE + lz;
                cd.biomes[chunkBiomeIndex(lx, lz)] = (uint8_t)Biome::PLAINS;
                for (int y = RY0; y < RY1; y++) {
                    if (wx % 8 == 0 && y % 8 == 0 && wz % 8 == 0)
                        cd.blocks[chunkBlockIndex(lx, y, lz)] = (uint8_t)Block::STONE;
                    else
                        cd.blocks[chunkBlockIndex(lx, y, lz)] = (uint8_t)Block::AIR;
                }
            }
        }
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int skyLight = 15;
                for (int y = RY1 - 1; y >= RY0; y--) {
                    Block b = (Block)cd.blocks[chunkBlockIndex(lx, y, lz)];
                    if (!isTransparent(b) && b != Block::GLASS) skyLight = 0;
                    else if (b == Block::WATER) skyLight = max(0, skyLight - 2);
                    cd.lightMap[chunkBlockIndex(lx, y, lz)] = (uint8_t)skyLight;
                }
            }
        }
        return;
    }

    // ============================================================
    // Pass 1: 每列采样 5 参数 → 群系 → 目标高度（不再做 9x9 平滑）
    // ============================================================
    // ============================================================
    // 高度图（原版）：24×24 采样 + 9×9×4 轮平滑
    // ============================================================
    const int CHUNK_EXT = 4;
    const int HW = CHUNK_SIZE + 2 * CHUNK_EXT;
    static thread_local vector<float> heights;
    heights.assign(HW * HW, 0.0f);
    computeChunkHeight(cx, cz, heights.data());

    float centerH[CHUNK_SIZE][CHUNK_SIZE];
    for (int lz = 0; lz < CHUNK_SIZE; lz++)
        for (int lx = 0; lx < CHUNK_SIZE; lx++)
            centerH[lz][lx] = heights[(lz + CHUNK_EXT) * HW + (lx + CHUNK_EXT)];

    // 群系采样
            // 群系采样
    static thread_local float contArr[CHUNK_SIZE * CHUNK_SIZE];
    static thread_local float eroArr[CHUNK_SIZE * CHUNK_SIZE];
    for (int lz = 0; lz < CHUNK_SIZE; lz++)
        for (int lx = 0; lx < CHUNK_SIZE; lx++)
        {
            int wx = cx * CHUNK_SIZE + lx;
            int wz = cz * CHUNK_SIZE + lz;
            TerrainOut tm = terrainModel((float)wx, (float)wz);
            cd.biomes[chunkBiomeIndex(lx, lz)] = (uint8_t)tm.b;
            // ★ 判据改读 MC 噪声(与 mcSurfaceHeight 完全同源),域 [-1,1]
            contArr[lz * CHUNK_SIZE + lx] =
                mcShifted((float)wx, 0.f, (float)wz, mcCont, mcShiftX, mcShiftZ);
            eroArr[lz * CHUNK_SIZE + lx] =
                mcShifted((float)wx, 0.f, (float)wz, mcEro, mcShiftX, mcShiftZ);
        }




    // ============================================================
    // 按列填充（原版）：实体 + 涂装 + 填水 + 冰 + 水下顶面
    // ============================================================
    for (int lz = 0; lz < CHUNK_SIZE; lz++)
        for (int lx = 0; lx < CHUNK_SIZE; lx++)
        {
            int wx = cx * CHUNK_SIZE + lx;
            int wz = cz * CHUNK_SIZE + lz;
            Biome b = (Biome)cd.biomes[chunkBiomeIndex(lx, lz)];
            int surface = (int)centerH[lz][lx];
            if (g_modSettings.loaded && g_modSettings.noHeightLimit) {
                surface = max(RY0 + 5, surface);
                if (surface >= RY1 - 1) surface = RY1 - 2;
            }
            else {
                surface = max(RY0 + 5, min(RY1 - 8, surface));
            }

            // ---- 河流 ----
            float rv = G::nRiver.fbm((float)wx * 0.0010f, (float)wz * 0.0010f, 2);
            float ridge = 1.0f - fabsf(rv * 2.0f - 1.0f);
            float widthN = 0.5f + 0.5f * G::nWeird.sample((float)wx * 0.003f, (float)wz * 0.003f);
            float wThresh = 0.94f - 0.05f * widthN;
            float land = tmSat((contArr[lz * CHUNK_SIZE + lx] + 0.16f) / 0.10f);
            float slopeGate = tmSat((eroArr[lz * CHUNK_SIZE + lx] + 0.1f) / 0.4f);
            float riverMask = tmSat((ridge - wThresh) / 0.045f) * land * slopeGate;
            if (riverMask > 0.0f) {
                float core = riverMask * riverMask * (3.0f - 2.0f * riverMask);
                surface -= (int)(core * (4.0f + 3.0f * widthN));
                float bankBand = tmSat((ridge - (wThresh - 0.05f)) / 0.05f) * land * slopeGate;
                float bank = bankBand * (1.0f - riverMask);
                if (bank > 0.0f && surface > SEA_LEVEL + 2)
                    surface -= (int)(bank * 2.5f);
            }
            if (riverMask > 0.05f && surface <= SEA_LEVEL + 1)
                surface = SEA_LEVEL - 2;
            if (riverMask > 0.45f && contArr[lz * CHUNK_SIZE + lx] >= -0.16f) {
                BiomeProfile po = biomeProfile(b);
                b = po.snowy ? Biome::FROZEN_RIVER : Biome::RIVER;
                cd.biomes[chunkBiomeIndex(lx, lz)] = (uint8_t)b;
            }

            // ★ 补丁一·位置1:isSea 判定 + 陆侧钳制(必须在涂装循环之前)
            bool isSea = contArr[lz * CHUNK_SIZE + lx] < -0.16f;
            if (!isSea && riverMask <= 0.05f && surface < SEA_LEVEL + 1)
                surface = SEA_LEVEL + 1;

            BiomeProfile p = biomeProfile(b);
            int yLow = stub ? max(RY0, surface - 20) : RY0; // ★ 桩:地表下 20 格
            for (int y = yLow; y <= surface; y++) {

                Block blk;
                if (y < -56) blk = Block::DEEPSLATE;
                else if (y < 8) blk = (rand01_3(wx, y, wz, seed32(0xAB)) < 0.15f) ? Block::TUFF : Block::DEEPSLATE;
                else blk = Block::STONE;
                if (y == surface) {
                    blk = p.surface;
                    if (p.snowy && y >= 68) blk = Block::SNOW_BLOCK;
                }
                else if (y > surface - 4) blk = p.subsurface;
                if (b == Biome::DESERT && y > surface - 6 && y < surface) blk = Block::SANDSTONE;
                if ((b == Biome::BADLANDS || b == Biome::ERODED_BADLANDS || b == Biome::WOODED_BADLANDS) && y > surface - 8 && y < surface)
                    blk = (rand01_3(wx, y, wz, seed32(0xCD)) < 0.5f) ? Block::RED_SANDSTONE : Block::TERRACOTTA;
                cd.blocks[chunkBlockIndex(lx, y, lz)] = (uint8_t)blk;
            }

            // ★ 补丁一·位置2:灌水(isSea 判海,与高度场同源)
            if (isSea) {
                for (int y = surface + 1; y <= SEA_LEVEL; y++)
                    if (cd.blocks[chunkBlockIndex(lx, y, lz)] == (uint8_t)Block::AIR)
                        cd.blocks[chunkBlockIndex(lx, y, lz)] = (uint8_t)Block::WATER;
                if (p.snowy && cd.blocks[chunkBlockIndex(lx, SEA_LEVEL, lz)] == (uint8_t)Block::WATER)
                    cd.blocks[chunkBlockIndex(lx, SEA_LEVEL, lz)] = (uint8_t)Block::ICE;
            }
            else if (riverMask > 0.05f && surface < SEA_LEVEL) {
                for (int y = surface + 1; y <= SEA_LEVEL; y++)
                    if (cd.blocks[chunkBlockIndex(lx, y, lz)] == (uint8_t)Block::AIR)
                        cd.blocks[chunkBlockIndex(lx, y, lz)] = (uint8_t)Block::WATER;
                if (p.snowy && cd.blocks[chunkBlockIndex(lx, SEA_LEVEL, lz)] == (uint8_t)Block::WATER)
                    cd.blocks[chunkBlockIndex(lx, SEA_LEVEL, lz)] = (uint8_t)Block::ICE;
            }
            if (isSea && surface < SEA_LEVEL)
                cd.blocks[chunkBlockIndex(lx, surface, lz)] = (uint8_t)p.underwater;
        }   // ← 列循环闭合,一个不多一个不少

            // ← 列循环闭合,一个不多一个不少

    if (!stub) { // ★ 桩模式:跳过洞穴/峡谷/含水层/矿/树(靠近后按种子完整重生成)
    // ============================================================
    // 峡谷后挖(原版行为:地表裂缝)
    // ============================================================
        carveRavines(cd, cx, cz);





    // ============================================================
    // 洞穴（“好点了”整合版：疏密掩码 + 意面下限）
    // ============================================================
    // ============================================================
    // 洞穴（格点采样 + 三线性插值：噪声计算量降 ~25 倍，MC density 思路）
    // ============================================================
    {
        const Tuning& t = g_tuning;
        float cFreq = t.cheeseFreq;
        float cVFreq = t.cheeseFreq * 1.5f;
        const float spagFreqXZ = 0.012f;
        const float spagFreqY = 0.018f;
        const float noodleFreqXZ = 0.024f;
        const float noodleFreqY = 0.034f;

        const int LX = 5, LZ = 5;          // XZ 每 4 格一个格点：0,4,8,12,16
        const int Y_STEP = 8;
        const int yBotC = RY0 + 3, yTopC = 110;
        const int LY = (yTopC - yBotC) / Y_STEP + 2;

        static thread_local vector<float> latC1, latC2, latS1, latS2, latN1, latN2;
        latC1.assign((size_t)LX * LZ * LY, 0.f);
        latC2.assign(latC1.size(), 0.f);
        latS1.assign(latC1.size(), 0.f);
        latS2.assign(latC1.size(), 0.f);
        latN1.assign(latC1.size(), 0.f);
        latN2.assign(latC1.size(), 0.f);

        auto latIdx = [&](int ix, int iy, int iz) { return ((size_t)ix * LZ + iz) * LY + iy; };


        for (int iy = 0; iy < LY; iy++) {
            float y = (float)(yBotC + iy * Y_STEP);
            for (int iz = 0; iz < LZ; iz++) {
                float wz = (float)(cz * CHUNK_SIZE + iz * 4);
                for (int ix = 0; ix < LX; ix++) {
                    float wx = (float)(cx * CHUNK_SIZE + ix * 4);
                    size_t k = latIdx(ix, iy, iz);
                    latC1[k] = G::nCheese.fbm(wx * cFreq, y * cVFreq, wz * cFreq, 2);
                    latC2[k] = G::nCheese2.fbm(wx * cFreq, y * cVFreq, wz * cFreq, 2);
                    latS1[k] = G::nSpag.fbm(wx * spagFreqXZ, y * spagFreqY, wz * spagFreqXZ, 2);
                    latS2[k] = G::nSpag2.fbm(wx * spagFreqXZ, y * spagFreqY, wz * spagFreqXZ, 2);
                    latN1[k] = G::nNoodle.fbm(wx * noodleFreqXZ, y * noodleFreqY, wz * noodleFreqXZ, 2);
                    latN2[k] = G::nNoodle2.fbm(wx * noodleFreqXZ, y * noodleFreqY, wz * noodleFreqXZ, 2);
                }
            }
        }

        // 局部坐标 (lx 0..15, lz 0..15) 的三线性插值
        auto tri = [&](const vector<float>& lat, float lx, float y, float lz) {
            float fx = lx * 0.25f, fz = lz * 0.25f, fy = (y - (float)yBotC) / (float)Y_STEP;
            int x0 = (int)fx, z0 = (int)fz, y0 = (int)fy;
            if (x0 > LX - 2) x0 = LX - 2;
            if (z0 > LZ - 2) z0 = LZ - 2;
            if (y0 < 0) y0 = 0;
            if (y0 > LY - 2) y0 = LY - 2;
            float tx = fx - (float)x0, tz = fz - (float)z0, ty = fy - (float)y0;
            float c000 = lat[latIdx(x0, y0, z0)], c100 = lat[latIdx(x0 + 1, y0, z0)];
            float c010 = lat[latIdx(x0, y0 + 1, z0)], c110 = lat[latIdx(x0 + 1, y0 + 1, z0)];
            float c001 = lat[latIdx(x0, y0, z0 + 1)], c101 = lat[latIdx(x0 + 1, y0, z0 + 1)];
            float c011 = lat[latIdx(x0, y0 + 1, z0 + 1)], c111 = lat[latIdx(x0 + 1, y0 + 1, z0 + 1)];
            float a = c000 + (c100 - c000) * tx;
            float b = c010 + (c110 - c010) * tx;
            float c = c001 + (c101 - c001) * tx;
            float d = c011 + (c111 - c011) * tx;
            float e = a + (b - a) * ty;
            float f = c + (d - c) * ty;
            return e + (f - e) * tz;
        };

        for (int lz = 0; lz < CHUNK_SIZE; lz++)
            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                int surfaceY = (int)centerH[lz][lx];
                int wxo = cx * CHUNK_SIZE + lx, wzo = cz * CHUNK_SIZE + lz;
                int caveCeil = surfaceY - 6;
                if (rand01_3(wxo, 777, wzo, seed32(0xBEA7)) < 0.15f)   // 15% 列:洞口穿透地表
                    caveCeil = surfaceY + 2;

                if (caveCeil > 110) caveCeil = 110;
                if (caveCeil < RY0 + 4) caveCeil = RY0 + 3;
                int yTop = caveCeil, yBot = RY0 + 3;
                int ySpan = max(1, yTop - yBot);
                float caveMask = G::nDetail.sample((float)(cx * CHUNK_SIZE + lx) * 0.004f,
                    (float)(cz * CHUNK_SIZE + lz) * 0.004f);
                float densityMul = 0.50f + 1.00f * caveMask;
                for (int y = yBot; y <= yTop; y++) {
                    size_t idx = chunkBlockIndex(lx, y, lz);
                    Block b = (Block)cd.blocks[idx];
                    if (b == Block::AIR || b == Block::WATER || b == Block::LAVA) continue;
                    float depthFrac = (float)(yTop - y) / (float)ySpan;
                    depthFrac = max(0.0f, min(1.0f, depthFrac));
                    bool cave = false;
                    if (depthFrac > 0.25f) {
                        float c1 = tri(latC1, (float)lx, (float)y, (float)lz) - 0.5f;
                        float c2 = tri(latC2, (float)lx, (float)y, (float)lz) - 0.5f;
                        float t0 = (depthFrac - 0.25f) / 0.75f;
                        float th = (0.00065f + 0.00090f * t0) * 1.3f * densityMul;
                        if (y < -48) th *= 0.5f;
                        if (c1 * c1 + c2 * c2 < th) cave = true;
                    }
                    if (!cave) {
                        float th = max(0.00014f, (0.00018f + 0.00022f * depthFrac) * densityMul);
                        float s1 = tri(latS1, (float)lx, (float)y, (float)lz) - 0.5f;
                        float s2 = tri(latS2, (float)lx, (float)y, (float)lz) - 0.5f;
                        if (s1 * s1 + s2 * s2 < th) cave = true;
                    }
                    if (!cave) {
                        float th = max(0.00009f, (0.00012f + 0.00020f * depthFrac) * densityMul);
                        float n1 = tri(latN1, (float)lx, (float)y, (float)lz) - 0.5f;
                        float n2 = tri(latN2, (float)lx, (float)y, (float)lz) - 0.5f;
                        if (n1 * n1 + n2 * n2 < th) cave = true;
                    }
                    if (cave) cd.blocks[idx] = (uint8_t)Block::AIR;
                }
            }
    }



    // ===== 岩石变种斑块（原样保留）=====
    {
        Rng rng(chunkRngSeed(cx, cz) ^ 0x1234567890ULL);
        for (int k = 0; k < 4; k++) {
            int px = rng.range(0, 15), pz = rng.range(0, 15);
            int py = rng.range(RY0 + 5, 50), r = rng.range(3, 7);
            Block variant = (Block)rng.range((int)Block::ANDESITE, (int)Block::GRANITE);
            for (int dx = -r; dx <= r; dx++)
            for (int dy = -r; dy <= r; dy++)
            for (int dz = -r; dz <= r; dz++) {
                if (dx*dx + dy*dy + dz*dz > r*r) continue;
                int qx = px+dx, qz = pz+dz;
                if (qx < 0 || qx >= CHUNK_SIZE || qz < 0 || qz >= CHUNK_SIZE) continue;
                int qy = py+dy;
                if (qy < RY0 || qy >= RY1) continue;
                Block bb = (Block)cd.blocks[chunkBlockIndex(qx, qy, qz)];
                if (bb == Block::STONE || bb == Block::DEEPSLATE)
                    cd.blocks[chunkBlockIndex(qx, qy, qz)] = (uint8_t)variant;
            }
        }
    }

    // ===== 矿洞 tubes（原样 + 一行地表保护）=====
    // ===== 真含水层(euc.java 移植):只处理地表以下、海平面以下的空腔 =====
    {
        int wx0 = cx * CHUNK_SIZE, wz0 = cz * CHUNK_SIZE;
        Aquifer aqu(wx0, wx0 + CHUNK_SIZE - 1, wz0, wz0 + CHUNK_SIZE - 1, RY0, RH);
        for (int lz = 0; lz < CHUNK_SIZE; lz++)
            for (int lx = 0; lx < CHUNK_SIZE; lx++)
            {
                int wx = wx0 + lx, wz = wz0 + lz;
                int surf = (int)centerH[lz][lx];
                int yTop = surf - 1; if (yTop > SEA_LEVEL - 1) yTop = SEA_LEVEL - 1;
                for (int y = RY0 + 1; y <= yTop; y++)
                {
                    size_t idx = chunkBlockIndex(lx, y, lz);
                    if (cd.blocks[idx] != (uint8_t)Block::AIR) continue;
                    AqFluid f = aqu.computeSubstance(wx, y, wz, -1.0);
                    if (f == AQ_WATER) cd.blocks[idx] = (uint8_t)Block::WATER;
                    else if (f == AQ_LAVA)  cd.blocks[idx] = (uint8_t)Block::LAVA;
                }
            }
    }

    //}



    for (int lz = 0; lz < CHUNK_SIZE; lz++) for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        int wx = cx * CHUNK_SIZE + lx;
        int wz = cz * CHUNK_SIZE + lz;

        
        for (int y = RY0 + 1; y < 0; y++) {
            size_t idx = chunkBlockIndex(lx, y, lz);
            if (cd.blocks[idx] != (uint8_t)Block::AIR) continue;
            float l = G::nLava.fbm(wx*0.03f, y*0.03f, wz*0.03f, 2);
            if (y < -54 || (y < -30 && l > 0.65f))
                cd.blocks[idx] = (uint8_t)Block::LAVA;
        }
    }

    for (int lz = 0; lz < CHUNK_SIZE; lz++) for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int y = RY1 - 2; y >= RY0; y--) {
            size_t idx = chunkBlockIndex(lx, y, lz);
            if (cd.blocks[idx] != (uint8_t)Block::AIR) continue;
            if (y > SEA_LEVEL) continue;
            Block above = (Block)cd.blocks[chunkBlockIndex(lx, y + 1, lz)];
            if (above == Block::WATER) {
                cd.blocks[idx] = (uint8_t)Block::WATER;
            }
        }
    }

    {
        Rng rng(chunkRngSeed(cx, cz) ^ 0x55AA55AAULL);
        struct OreSpec { Block ore; int minY, maxY, veins, size; };
        static const OreSpec specs[] = {
            { Block::COAL_ORE,      5, 200, 20, 17 },
            { Block::IRON_ORE,    -24,  56, 12,  9 },
            { Block::COPPER_ORE,  -16, 112, 10,  9 },
            { Block::GOLD_ORE,    -60,  32,  4,  6 },
            { Block::REDSTONE_ORE,-60,  15,  5,  8 },
            { Block::LAPIS_ORE,   -60,  30,  3,  7 },
            { Block::DIAMOND_ORE, -60,  16,  2,  5 },
            { Block::EMERALD_ORE, -16, 196,  1,  4 },
        };
        for (auto& s : specs) {
            for (int i = 0; i < s.veins; i++) {
                int x = rng.range(2, 13), z = rng.range(2, 13);
                int y = rng.range(s.minY, s.maxY);
                int size = rng.range(3, s.size);
                for (int k = 0; k < size; k++) {
                    if (x >= 0 && x < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE
                        && y >= RY0 && y < RY1) {
                        size_t idx = chunkBlockIndex(x, y, z);
                        Block b = (Block)cd.blocks[idx];
                        if (b == Block::STONE || b == Block::DEEPSLATE ||
                            b == Block::ANDESITE || b == Block::DIORITE ||
                            b == Block::GRANITE || b == Block::TUFF)
                            cd.blocks[idx] = (uint8_t)s.ore;
                    }
                    int d = rng.range(0, 5);
                    if (d == 0) x++; else if (d == 1) x--;
                    else if (d == 2) y++; else if (d == 3) y--;
                    else if (d == 4) z++; else z--;
                }
            }
        }
    }

    {
        Rng rng(chunkRngSeed(cx, cz) ^ 0xAA55AA55ULL);
        for (int lx = 2; lx < CHUNK_SIZE - 2; lx++) for (int lz = 2; lz < CHUNK_SIZE - 2; lz++) {
            if (rng.f01() > 0.012f) continue;
            int wx = cx * CHUNK_SIZE + lx, wz = cz * CHUNK_SIZE + lz;
            Biome b = (Biome)cd.biomes[chunkBiomeIndex(lx, lz)];
            BiomeProfile p = biomeProfile(b);
            if (p.ocean || b == Biome::RIVER || b == Biome::FROZEN_RIVER) continue;

            int surface = -1;
            for (int y = RY1 - 2; y > RY0; y--) {
                Block blk = (Block)cd.blocks[chunkBlockIndex(lx, y, lz)];
                if (blk == Block::GRASS || blk == Block::SAND || blk == Block::SNOW_BLOCK
                    || blk == Block::PODZOL || blk == Block::MYCELIUM || blk == Block::RED_SAND) {
                    surface = y; break;
                }
            }
            if (surface < 0 || surface < SEA_LEVEL) continue;
            if (inRavine(wx, surface + 1, wz)) continue;

            Block logBlk = Block::OAK_LOG, leafBlk = Block::OAK_LEAVES;
            if (b == Biome::TAIGA || b == Biome::SNOWY_TAIGA || b == Biome::OLD_GROWTH_TAIGA) {
                logBlk = Block::SPRUCE_LOG; leafBlk = Block::SPRUCE_LEAVES;
            } else if (b == Biome::BIRCH_FOREST) {
                logBlk = Block::BIRCH_LOG; leafBlk = Block::BIRCH_LEAVES;
            } else if (b == Biome::JUNGLE || b == Biome::SPARSE_JUNGLE || b == Biome::BAMBOO_JUNGLE) {
                logBlk = Block::JUNGLE_LOG; leafBlk = Block::JUNGLE_LEAVES;
            } else if (b == Biome::DARK_FOREST) {
                logBlk = Block::DARK_OAK_LOG; leafBlk = Block::DARK_OAK_LEAVES;
            } else if (b == Biome::SAVANNA || b == Biome::SAVANNA_PLATEAU || b == Biome::WINDSWEPT_SAVANNA) {
                logBlk = Block::ACACIA_LOG; leafBlk = Block::ACACIA_LEAVES;
            }

            int h = rng.range(4, 7);
            if (surface + h + 3 >= RY1) continue;
            for (int i = 0; i < h; i++) {
                if (surface + 1 + i < RY1)
                    cd.blocks[chunkBlockIndex(lx, surface + 1 + i, lz)] = (uint8_t)logBlk;
            }
            int top = surface + h;
            int rr = (b == Biome::JUNGLE || b == Biome::DARK_FOREST) ? 3 : 2;
            for (int dx = -rr; dx <= rr; dx++) for (int dz = -rr; dz <= rr; dz++) for (int dy = -2; dy <= 1; dy++) {
                if (dx*dx + dz*dz + dy*dy > rr*rr + 1) continue;
                int qx = lx+dx, qz = lz+dz;
                if (qx < 0 || qx >= CHUNK_SIZE || qz < 0 || qz >= CHUNK_SIZE) continue;
                int qy = top + dy;
                if (qy < RY0 || qy >= RY1) continue;
                size_t idx = chunkBlockIndex(qx, qy, qz);
                if (cd.blocks[idx] == (uint8_t)Block::AIR)
                    cd.blocks[idx] = (uint8_t)leafBlk;
            }
        }
    }
    } // ★ if (!stub) 收口

    // 生成完毕，执行 BFS 计算光照！
    //CalculateLightBFS(cd, cx, cz);
}


// ============================================================
// Section 6.5. .map 存档系统
// ============================================================
namespace Save {

    static const char MAGIC[8] = { 'V','O','X','M','A','P','1','0' };
    static const uint32_t VERSION = 10;
    // ---- RLE：连续同值方块 → (block,run)，地形数据通常缩 30~100 倍 ----
    static void rleEncode(const vector<uint8_t>& in, vector<uint8_t>& out) {
        out.clear();
        out.reserve(in.size() / 8 + 256);
        size_t i = 0;
        while (i < in.size()) {
            uint8_t b = in[i];
            size_t j = i + 1;
            while (j < in.size() && in[j] == b && (j - i) < 0xFFFF) j++;
            uint16_t run = (uint16_t)(j - i);
            out.push_back(b); out.push_back(0);
            out.push_back((uint8_t)(run & 0xFF));
            out.push_back((uint8_t)(run >> 8));
            i = j;
        }
    }
    static bool rleDecode(const vector<uint8_t>& in, size_t expect, vector<uint8_t>& out) {
        out.assign(expect, 0);
        size_t o = 0;
        for (size_t i = 0; i + 4 <= in.size(); i += 4) {
            uint16_t run = (uint16_t)(in[i + 2] | ((uint16_t)in[i + 3] << 8));
            if (o + run > expect) return false;
            memset(out.data() + o, in[i], run);
            o += run;
        }
        return o == expect;
    }


static const uint32_t RECORD_TAG = 0xC4A40BEDu;

struct FileHeader {
    char     magic[8];
    uint32_t version;
    uint32_t _pad;
    uint64_t seed;
    Tuning   tuning;
    int32_t  spawnX, spawnY, spawnZ;
    int32_t  _pad2;
};

struct ChunkRecord {
    uint32_t tag;
    int32_t  cx;
    int32_t  cz;
    uint32_t byteCount;
};

class Store {
public:
    FILE*    fp = nullptr;
    wstring  path;
    unordered_map<uint64_t, uint64_t> chunkOffsets;
    unordered_map<uint64_t, uint32_t> chunkSizes;   // ★ 每条记录的数据区长度(原地覆写用)
    mutex    ioMutex;

    bool open(const wstring& p, bool createIfMissing) {
        path = p;
        fp = _wfopen(p.c_str(), L"r+b");
        if (!fp && createIfMissing) {
            fp = _wfopen(p.c_str(), L"w+b");
            if (!fp) return false;
            writeHeader();
            return true;
        }
        if (!fp) return false;
        if (!readHeader()) {
            fclose(fp);
            fp = nullptr;
            return false;
        }
        scanIndex();
        return true;
    }

    void close() {
        if (fp) { fflush(fp); fclose(fp); fp = nullptr; }
    }

    ~Store() { close(); }

    bool writeChunk(int cx, int cz, const vector<uint8_t>& blocks, const vector<uint8_t>& biomes) {
        if (!fp) return false;
        if (blocks.size() != CHUNK_BLOCK_COUNT || biomes.size() != CHUNK_BIOME_COUNT) return false;
        lock_guard<mutex> lk(ioMutex);
        vector<uint8_t> encB, encBi;
        rleEncode(blocks, encB);
        rleEncode(biomes, encBi);
        uint32_t nb = (uint32_t)encB.size();
        uint32_t ni = (uint32_t)encBi.size();
        uint64_t key = chunkKey(cx, cz);
        uint32_t newSize = 8 + nb + ni;

        // ★ 关键:已存在的区块【原地覆写】,不再无脑追加。
        //   旧实现每次存档都往文件尾追加一份,长时间游玩 .map 会膨胀到几十 GB。
        auto it = chunkOffsets.find(key);
        if (it != chunkOffsets.end()) {
            uint32_t oldSize = chunkSizes[key];
            if (newSize <= oldSize) {
                _fseeki64(fp, (long long)it->second, SEEK_SET);   // 指向 nb 字段
                fwrite(&nb, 4, 1, fp);
                fwrite(&ni, 4, 1, fp);
                fwrite(encB.data(), 1, nb, fp);
                fwrite(encBi.data(), 1, ni, fp);
                return true;   // 剩余旧字节不会被读取(nb/ni 决定长度)
            }
            // 新数据更大:只能追加,旧空间作废(低频,可接受)
        }
        _fseeki64(fp, 0, SEEK_END);
        uint64_t off = (uint64_t)_ftelli64(fp);
        ChunkRecord rec = { RECORD_TAG, cx, cz, newSize };
        fwrite(&rec, sizeof(rec), 1, fp);
        fwrite(&nb, 4, 1, fp);
        fwrite(&ni, 4, 1, fp);
        fwrite(encB.data(), 1, nb, fp);
        fwrite(encBi.data(), 1, ni, fp);
        chunkOffsets[key] = off + sizeof(rec);
        chunkSizes[key]   = newSize;
        return true;
    }
    bool readChunk(int cx, int cz, G::ChunkData& cd) {
        if (!fp) return false;
        lock_guard<mutex> lk(ioMutex);
        auto it = chunkOffsets.find(chunkKey(cx, cz));
        if (it == chunkOffsets.end()) return false;
        _fseeki64(fp, (long long)it->second, SEEK_SET);
        uint32_t nb = 0, ni = 0;
        if (fread(&nb, 4, 1, fp) != 1 || fread(&ni, 4, 1, fp) != 1) return false;
        if (nb > CHUNK_BLOCK_COUNT * 4 + 64 || ni > CHUNK_BIOME_COUNT * 4 + 64) return false;
        vector<uint8_t> encB(nb), encBi(ni), raw;
        if (nb && fread(encB.data(), 1, nb, fp) != nb) return false;
        if (ni && fread(encBi.data(), 1, ni, fp) != ni) return false;
        if (!rleDecode(encB, CHUNK_BLOCK_COUNT, raw)) return false;
        memcpy(cd.blocks.data(), raw.data(), CHUNK_BLOCK_COUNT);
        if (!rleDecode(encBi, CHUNK_BIOME_COUNT, raw)) return false;
        memcpy(cd.biomes.data(), raw.data(), CHUNK_BIOME_COUNT);
        return true;
    }


    void flush() { if (fp) fflush(fp); }

private:
    bool writeHeader() {
        if (!fp) return false;
        FileHeader h = {};
        memcpy(h.magic, MAGIC, 8);
        h.version = VERSION;
        h.seed = G::seed;
        h.tuning = g_tuning;
        h.spawnX = (int32_t)G::camPos.x;
        h.spawnY = (int32_t)G::camPos.y;
        h.spawnZ = (int32_t)G::camPos.z;
        _fseeki64(fp, 0, SEEK_SET);
        fwrite(&h, sizeof(h), 1, fp);
        return true;
    }

    bool readHeader() {
        if (!fp) return false;
        _fseeki64(fp, 0, SEEK_SET);
        FileHeader h;
        if (fread(&h, sizeof(h), 1, fp) != 1) return false;
        if (memcmp(h.magic, MAGIC, 8) != 0) return false;
        if (h.version != VERSION) {
            printf("[SAVE] Version mismatch (%u vs %u)\n", h.version, VERSION);
            return false;
        }
        G::seed = h.seed;
        g_tuning = h.tuning;
        reseedAllNoise(G::seed);
        //generateCanyons(G::seed);
        generateTubes(G::seed);
        G::camPos = XMFLOAT3((float)h.spawnX + 0.5f,
                              (float)h.spawnY,
                              (float)h.spawnZ + 0.5f);
        printf("[SAVE] Header OK seed=%llX spawn=(%d,%d,%d)\n",
               (unsigned long long)G::seed, h.spawnX, h.spawnY, h.spawnZ);
        return true;
    }

    void scanIndex() {
        chunkOffsets.clear();
        chunkSizes.clear();
        _fseeki64(fp, (long long)sizeof(FileHeader), SEEK_SET);
        ChunkRecord rec;
        while (fread(&rec, sizeof(rec), 1, fp) == 1) {
            if (rec.tag != RECORD_TAG) break;
            int64_t dataPos = _ftelli64(fp);
            uint64_t key = chunkKey(rec.cx, rec.cz);
            chunkOffsets[key] = (uint64_t)dataPos;
            chunkSizes[key]   = rec.byteCount;
            _fseeki64(fp, (long long)rec.byteCount, SEEK_CUR);
        }
        printf("[SAVE] Indexed %zu chunks.\n", chunkOffsets.size());
    }
};

static Store g_store;

} // namespace Save

// ============================================================
// Section 7. 网格构建
// ============================================================
struct Vertex {
    XMFLOAT3 pos;
    XMFLOAT3 normal;
    XMFLOAT3 color;
    XMFLOAT2 uv;
    float    texLayer;
    float    shade;
    float    light; // ★ 新增：光照强度
    float cave;
};
struct MeshData {
    vector<Vertex>   vertices;
    vector<uint32_t> indices;
};

static const int FACE_DIRS[6][3] = {
    { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1}
};
static const float FACE_VERTS[6][4][3] = {
    {{1,0,1},{1,0,0},{1,1,0},{1,1,1}},
    {{0,0,0},{0,0,1},{0,1,1},{0,1,0}},
    {{0,1,1},{1,1,1},{1,1,0},{0,1,0}},
    {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
    {{0,0,1},{1,0,1},{1,1,1},{0,1,1}},
    {{1,0,0},{0,0,0},{0,1,0},{1,1,0}}
};

static const float FACE_UVS[4][2] = {
    {0.0f, 1.0f},
    {1.0f, 1.0f},
    {1.0f, 0.0f},
    {0.0f, 0.0f},
};

struct ChunkCache {
    shared_ptr<G::ChunkData> data[3][3];
    int baseCx = 0, baseCz = 0;
    bool allLoaded = false;

    void load(int cx, int cz) {
        baseCx = cx - 1;
        baseCz = cz - 1;
        lock_guard<mutex> lk(G::chunksMutex);
        for (int dz = 0; dz < 3; dz++)
            for (int dx = 0; dx < 3; dx++) {
                auto it = G::chunks.find(chunkKey(cx + dx - 1, cz + dz - 1));
                data[dx][dz] = (it != G::chunks.end()) ? it->second : nullptr;
            }
        allLoaded = (data[1][1] != nullptr);   // 中心块在即可建；缺的邻居按空气处理，到位后自动重建
    }

    Block get(int wx, int wy, int wz) const {
        if (wy < RY0 || wy >= RY1) return Block::AIR;
        int cx = toChunk(wx), cz = toChunk(wz);
        int dx = cx - baseCx, dz = cz - baseCz;
        if (dx < 0 || dx > 2 || dz < 0 || dz > 2) return Block::AIR;
        G::ChunkData* cd = data[dx][dz].get();
        if (!cd) return Block::AIR;
        return (Block)cd->blocks[chunkBlockIndex(localChunk(wx), wy, localChunk(wz))];
    }

    uint16_t getTop(int wx, int wz) const {
        int cx = toChunk(wx), cz = toChunk(wz);
        int dx = cx - baseCx, dz = cz - baseCz;
        if (dx < 0 || dx > 2 || dz < 0 || dz > 2) return (uint16_t)(RY1 - 1 - RY0);
        G::ChunkData* cd = data[dx][dz].get();
        if (!cd || cd->colTop.size() != (size_t)CHUNK_AREA) return (uint16_t)(RY1 - 1 - RY0);
        return cd->colTop[chunkBiomeIndex(localChunk(wx), localChunk(wz))];
    }

    uint8_t getLight(int wx, int wy, int wz) const {
        if (wy >= RY1) return 15;
        if (wy < RY0) return 0;
        int cx = toChunk(wx), cz = toChunk(wz);
        int dx = cx - baseCx, dz = cz - baseCz;
        if (dx < 0 || dx > 2 || dz < 0 || dz > 2) return 15;
        G::ChunkData* cd = data[dx][dz].get();
        if (!cd) return 15;
        return cd->lightMap[chunkBlockIndex(localChunk(wx), wy, localChunk(wz))];
    }
};


static float computeAO(const ChunkCache& cc, int x, int y, int z, int fx, int fy, int fz) {
    int s1 = isOpaqueBlock(cc.get(x + fx, y + fy, z + fz)) ? 1 : 0;
    int s2 = isOpaqueBlock(cc.get(x + fx, y, z + fz)) ? 1 : 0;
    int s3 = isOpaqueBlock(cc.get(x, y + fy, z + fz)) ? 1 : 0;
    int v = s1 + s2 + s3;
    if (v >= 2) return 0.4f;
    return 1.0f - 0.15f * v;
}




//static bool buildChunkMeshCPU(int cx, int cz, ...) {   // ← 原有代码,不动


static bool buildChunkMeshCPU(int cx, int cz, bool opaquePass, MeshData& out) {
    ChunkCache cc;
    cc.load(cx, cz);
    if (!cc.allLoaded) return false;
    out.vertices.reserve(30000);      // ← 加这行
    out.indices.reserve(45000);       // ← 加这行
    //...


    int wx0 = cx * CHUNK_SIZE, wz0 = cz * CHUNK_SIZE;
    for (int lx = 0; lx < CHUNK_SIZE; lx++)
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int wx = wx0 + lx, wz = wz0 + lz;
            int topY = RY0 + (int)cc.getTop(wx, wz);   // ★ 只扫到列顶，不再扫满1024
            for (int y = RY0; y <= topY; y++) {
                Block b = cc.get(wx, y, wz);
                if (b == Block::AIR) continue;
                bool trans = isTransparent(b);
                if (opaquePass && trans) continue;
                if (!opaquePass && !trans) continue;
                BlockInfo& bi = g_blockInfo[(int)b];
                float baseR, baseG, baseB;
                if (b == Block::MISSING) {          // 黑白棋盘格:按世界坐标奇偶
                    bool dark = ((wx + y + wz) & 1) != 0;
                    baseR = baseG = baseB = dark ? 0.0f : 1.0f;
                }
                else {
                    BlockInfo& bi = g_blockInfo[(int)b];
                    baseR = bi.r / 255.0f; baseG = bi.g / 255.0f; baseB = bi.b / 255.0f;
                }

                for (int f = 0; f < 6; f++) {
                    int nx = wx + FACE_DIRS[f][0];
                    int ny = y + FACE_DIRS[f][1];
                    int nz = wz + FACE_DIRS[f][2];
                    Block nb = cc.get(nx, ny, nz);
                    if (!isTransparent(nb)) continue;
                    if (nb == b) continue;
                    //float faceLight = cc.getLight(nx, ny, nz) / 15.0f;
                    uint32_t baseIdx = (uint32_t)out.vertices.size();
                    float ao = computeAO(cc, wx, y, wz, FACE_DIRS[f][0], FACE_DIRS[f][1], FACE_DIRS[f][2]);
                    // 原来: (f==2) ? 1.0f : (f==3) ? 0.72f : 0.88f
                    float faceShade = (f == 2) ? 1.0f      // 顶
                        : (f == 3) ? 0.5f      // 底,MC 是 0.5
                        : (f == 0 || f == 1) ? 0.6f   // 东西(X 面)
                        : 0.8f;                        // 南北(Z 面)

                    float finalShade = ao * faceShade;
                    uint8_t texLayerByte = bi.tex[f];
                    float texLayerF = (texLayerByte == TEX_NONE) ? -1.0f : (float)texLayerByte;
                    for (int v = 0; v < 4; v++) {
                        // 顶点在世界里的绝对偏移(0/1),法线方向已含在 ax/ay/az 里
                                        // 该顶点在切向两轴上的偏移:法线轴为 0,其余轴按顶点 0/1 → -1/+1
                        int ox = (FACE_DIRS[f][0] != 0) ? 0 : (FACE_VERTS[f][v][0] ? 1 : -1);
                        int oy = (FACE_DIRS[f][1] != 0) ? 0 : (FACE_VERTS[f][v][1] ? 1 : -1);
                        int oz = (FACE_DIRS[f][2] != 0) ? 0 : (FACE_VERTS[f][v][2] ? 1 : -1);
                        // 恰好 ox/oy/oz 中两个非零(切向),构成顶点周围的 2x2 采样
                        int t[2][3]; int nt = 0;
                        if (ox) { t[nt][0] = ox; t[nt][1] = 0;  t[nt][2] = 0;  nt++; }
                        if (oy) { t[nt][0] = 0;  t[nt][1] = oy; t[nt][2] = 0;  nt++; }
                        if (oz) { t[nt][0] = 0;  t[nt][1] = 0;  t[nt][2] = oz; nt++; }
                        auto L = [&](int dx, int dy, int dz) {
                            return cc.getLight(nx + dx, ny + dy, nz + dz);
                        };
                        // MC smooth lighting:面正前空气格 + 该顶点贴着的 4 格(含对角)取平均
                        int sum = L(0, 0, 0)
                            + L(t[0][0], t[0][1], t[0][2])
                            + L(t[1][0], t[1][1], t[1][2])
                            + L(t[0][0] + t[1][0], t[0][1] + t[1][1], t[0][2] + t[1][2]);
                        float faceLight = sum / (4.0f * 15.0f);   // 沿用同名变量,vert.light = faceLight 不用动


                        Vertex vert;
                       // ...
                                  // 替换原 3×3 采样结果

                        vert.pos = XMFLOAT3(
                            (float)(wx + FACE_VERTS[f][v][0]),
                            (float)(y + FACE_VERTS[f][v][1]),
                            (float)(wz + FACE_VERTS[f][v][2]));
                        vert.normal = XMFLOAT3(
                            (float)FACE_DIRS[f][0], (float)FACE_DIRS[f][1], (float)FACE_DIRS[f][2]);
                        vert.color = XMFLOAT3(baseR, baseG, baseB);
                        vert.uv = XMFLOAT2(FACE_UVS[v][0], FACE_UVS[v][1]);
                        vert.texLayer = texLayerF;
                        vert.shade = finalShade;
                        vert.light = faceLight;

                        vert.cave = (float)((topY > y) ? (topY - y) : 0);   // ★ 地表=0,越深越大
                        out.vertices.push_back(vert);
                    }
                    out.indices.push_back(baseIdx + 0);
                    out.indices.push_back(baseIdx + 1);
                    out.indices.push_back(baseIdx + 2);
                    out.indices.push_back(baseIdx + 0);
                    out.indices.push_back(baseIdx + 2);
                    out.indices.push_back(baseIdx + 3);
                }
            }
        }
    return true;
}


// ============================================================
// Section 8. D3D11 资源
// ============================================================
struct ChunkMesh {
    ComPtr<ID3D11Buffer> vb;
    ComPtr<ID3D11Buffer> ib;
    uint32_t indexCount = 0;
    bool     valid = false;
};
struct ActiveChunk {
    ChunkMesh opaque;
    ChunkMesh trans;
};

// ★ 粒子顶点结构（必须在 initD3D 之前定义）
struct ParticleVertex {
    XMFLOAT3 center;
    float    size;
    XMFLOAT2 corner;
    XMFLOAT3 color;
    float    alpha;
};

static ComPtr<ID3D11Device>            g_device;
static ComPtr<ID3D11DeviceContext>     g_context;
static ComPtr<IDXGISwapChain>          g_swapChain;
static ComPtr<ID3D11RenderTargetView>  g_rtv;
static ComPtr<ID3D11DepthStencilView>  g_dsv;
static ComPtr<ID3D11Texture2D>         g_depthTex;

static ComPtr<ID3D11VertexShader>      g_vs;
static ComPtr<ID3D11PixelShader>       g_ps;
static ComPtr<ID3D11PixelShader>       g_psTrans;
static ComPtr<ID3D11InputLayout>       g_inputLayout;
static ComPtr<ID3D11Buffer>            g_cbuffer;

static ComPtr<ID3D11Texture2D>          g_texArray;
static ComPtr<ID3D11ShaderResourceView> g_texArraySRV;
static ComPtr<ID3D11SamplerState>       g_sampler;

static ComPtr<ID3D11DepthStencilState> g_dssOpaque;
static ComPtr<ID3D11DepthStencilState> g_dssTrans;
ComPtr<ID3D11DepthStencilState> g_skyDSS;   // 天空专用:测深度但不写深度

static ComPtr<ID3D11BlendState>        g_blendState;
static ComPtr<ID3D11RasterizerState>   g_rasterState;

// ★ 后处理资源
static ComPtr<ID3D11Texture2D>          g_offscreenTex;
static ComPtr<ID3D11RenderTargetView>   g_offscreenRTV;
static ComPtr<ID3D11ShaderResourceView> g_offscreenSRV;
static ComPtr<ID3D11Texture2D>          g_offscreenDepthTex;
static ComPtr<ID3D11DepthStencilView>   g_offscreenDSV;

static ComPtr<ID3D11VertexShader>       g_fsVS;
static ComPtr<ID3D11PixelShader>        g_fsPS;
static ComPtr<ID3D11SamplerState>       g_fsSampler;
static ComPtr<ID3D11Buffer>             g_fsVB;

static ComPtr<ID2D1Factory>          g_d2dFactory;
static ComPtr<IDWriteFactory>        g_dwFactory;
static ComPtr<IDWriteTextFormat>     g_textFormat;
static ComPtr<IDWriteTextFormat>     g_textFormatBottom;
static bool                          g_d2dAvailable = false;

// ★ 粒子资源
static ComPtr<ID3D11Buffer>            g_particleVB;
static ComPtr<ID3D11Buffer>            g_particleIB;
static ComPtr<ID3D11VertexShader>      g_particleVS;
static ComPtr<ID3D11PixelShader>       g_particlePS;
static ComPtr<ID3D11InputLayout>       g_particleLayout;
static ComPtr<ID3D11RasterizerState>   g_rasterNoCull;

// ===== 1. 全局区(现有 D3D 对象声明附近)=====
// ===== 1. 全局区(现有 D3D 对象声明附近)=====
static ID3D11Texture2D* g_giTex = nullptr;
static ID3D11ShaderResourceView* g_giSRV = nullptr;
static ID3D11Texture2D* g_shadowTex = nullptr;
static ID3D11ShaderResourceView* g_shadowSRV = nullptr;

// ================= Pass 0:太阳阴影深度 pass 资源 =================
static const int SHADOW_MAP_SIZE = 2048;
static const float SHADOW_RANGE = 96.0f;    // 正交范围 ±96 格(纹素更密,减少自阴影)
struct ShadowCB { XMMATRIX sunVP; XMFLOAT4 params; XMFLOAT4 sunScreen; XMFLOAT4 cloudParams; XMFLOAT4 jitter; };   // 128 B
static ID3D11Texture2D*          g_shadowDepthTex = nullptr;
static ID3D11DepthStencilView*   g_shadowDSV      = nullptr;
static ID3D11ShaderResourceView* g_shadowSRVMap   = nullptr;
static ID3D11SamplerState*       g_shadowSamp     = nullptr;
static ID3D11Buffer*             g_shadowCB       = nullptr;
static ID3D11VertexShader*       g_shadowVS       = nullptr;
static ID3D11InputLayout*        g_shadowLayout   = nullptr;

// ================= Pass 7/8:God rays + Bloom(半分辨率) =================
static ID3D11Texture2D*          g_godTex   = nullptr;
static ID3D11RenderTargetView*   g_godRTV   = nullptr;
static ID3D11ShaderResourceView* g_godSRV   = nullptr;
static ID3D11Texture2D*          g_bloomTex = nullptr;
static ID3D11RenderTargetView*   g_bloomRTV = nullptr;
static ID3D11ShaderResourceView* g_bloomSRV = nullptr;
static ID3D11PixelShader*        g_godPS    = nullptr;
static ID3D11PixelShader*        g_bloomPS  = nullptr;
// ================= Pass 5:云影 coverage(128×128,CPU 生成) =================
static const int CLOUD_SHADOW_RES = 128;
static const float CLOUD_SHADOW_SPAN = 2048.0f;   // 覆盖 ±1024 格
static ID3D11Texture2D*          g_cloudShadowTex = nullptr;
static ID3D11ShaderResourceView* g_cloudShadowSRV = nullptr;
static ID3D11SamplerState*       g_cloudShadowSamp = nullptr;
// ================= G4:平面反射(半分辨率) =================
static ID3D11Texture2D*          g_reflTex = nullptr;
static ID3D11RenderTargetView*   g_reflRTV = nullptr;
static ID3D11ShaderResourceView* g_reflSRV = nullptr;
static ID3D11DepthStencilView*   g_reflDSV = nullptr;

// ================= L:TAA 目标 + 历史 ping-pong =================
static ID3D11Texture2D*          g_taaTex = nullptr;
static ID3D11RenderTargetView*   g_taaRTV = nullptr;
static ID3D11ShaderResourceView* g_taaSRV = nullptr;
static ID3D11Texture2D*          g_histTex[2] = { nullptr, nullptr };
static ID3D11RenderTargetView*   g_histRTV[2] = { nullptr, nullptr };
static ID3D11ShaderResourceView* g_histSRV[2] = { nullptr, nullptr };
static int                       g_histIdx = 0;
static ID3D11PixelShader*        g_taaPS = nullptr;
static const bool TAA_ENABLE = false;   // ★ 关掉:无运动矢量的 TAA 会让地形边缘抖;要开改 true
static bool g_dbgCloud = true;          // F7:云 pass
static bool g_dbgSky   = true;          // F8:天空 pass
static bool g_dbgShadow= true;          // F9:太阳阴影 pass
static bool g_dbgFog   = true;          // F10:距离雾/高度雾

// ================= G3:深度副本(水面厚度) =================
static ID3D11Texture2D*          g_depthCopyTex = nullptr;
static ID3D11ShaderResourceView* g_depthCopySRV = nullptr;

// ===== GI 专用:每帧 chunk 快照(避免百万次加锁)=====
namespace cigi_support {
    static shared_ptr<G::ChunkData> snap[7][7];
    static int snapBaseX = 0, snapBaseZ = 0;
    void buildSnapshot(int wx, int wz) {
        snapBaseX = toChunk(wx) - 3; snapBaseZ = toChunk(wz) - 3;
        lock_guard<mutex> lk(G::chunksMutex);
        for (int dz = 0; dz < 7; dz++) for (int dx = 0; dx < 7; dx++) {
            auto it = G::chunks.find(chunkKey(snapBaseX + dx, snapBaseZ + dz));
            snap[dz][dx] = (it != G::chunks.end()) ? it->second : nullptr;
        }
    }
    int blockAtFast(int x, int y, int z) {
        if (y < RY0 || y >= RY1) return 0;
        int cx = toChunk(x) - snapBaseX, cz = toChunk(z) - snapBaseZ;
        if (cx < 0 || cx > 6 || cz < 0 || cz > 6) return 0;
        auto& p = snap[cz][cx];
        if (!p) return 0;
        int id = (int)p->blocks[chunkBlockIndex(localChunk(x), y, localChunk(z))];
        if (id && g_blockInfo[id].transparent) return 0;
        return id;
    }
}

void giInit(int rw, int rh) {

    cigi::init(rw, rh);
    cigi::s_query = cigi_support::blockAtFast;
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = rw / cigi::SCALE; d.Height = rh / cigi::SCALE;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.Usage = D3D11_USAGE_DYNAMIC;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateTexture2D(&d, nullptr, &g_giTex);
    g_device->CreateShaderResourceView(g_giTex, nullptr, &g_giSRV);

    D3D11_TEXTURE2D_DESC sd = d;
    sd.Format = DXGI_FORMAT_R8_UNORM;
    g_device->CreateTexture2D(&sd, nullptr, &g_shadowTex);
    g_device->CreateShaderResourceView(g_shadowTex, nullptr, &g_shadowSRV);
}

// ===== 2. 每帧调用(在你计算完 view/proj 之后、绘制方块之前)=====
void giFrame(XMMATRIX viewProj, XMFLOAT3 eye) {
    if (!g_giSRV) return;
    cigi_support::buildSnapshot((int)eye.x, (int)eye.z);
    cigi::setCamera(viewProj, eye);
    cigi::render();
    D3D11_MAPPED_SUBRESOURCE m;
    g_context->Map(g_giTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
    memcpy(m.pData, cigi::outRGBA, (size_t)cigi::W * cigi::H * 4);
    g_context->Unmap(g_giTex, 0);

    if (g_shadowTex) {
        D3D11_MAPPED_SUBRESOURCE sm;
        if (SUCCEEDED(g_context->Map(g_shadowTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &sm))) {
            for (int y = 0; y < cigi::H; y++)
                memcpy((unsigned char*)sm.pData + (size_t)y * sm.RowPitch,
                       cigi::shadowBuf + (size_t)y * cigi::W, (size_t)cigi::W);
            g_context->Unmap(g_shadowTex, 0);
        }
    }
}


// ===== 3. 绘制方块前绑定 =====
//ID3D11ShaderResourceView* srvs[1] = { g_giSRV };
//g_pImmContext->PSSetShaderResources(10, 1, srvs);      // 槽位 t10,避开你已用的


static unordered_map<uint64_t, ActiveChunk> g_activeMeshes;
static mutex g_meshMutex;

static unordered_set<uint64_t> g_meshesToRebuild;
static mutex                   g_rebuildMutex;

static bool uploadMeshToGPU(const MeshData& m, ChunkMesh& out) {
    if (m.vertices.empty() || m.indices.empty()) { out.valid = false; return false; }
    if (!g_device) return false;

    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.ByteWidth = (UINT)(m.vertices.size() * sizeof(Vertex));
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vd = {};
    vd.pSysMem = m.vertices.data();
    if (FAILED(g_device->CreateBuffer(&vbd, &vd, out.vb.ReleaseAndGetAddressOf())))
        return false;

    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.ByteWidth = (UINT)(m.indices.size() * sizeof(uint32_t));
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA id = {};
    id.pSysMem = m.indices.data();
    if (FAILED(g_device->CreateBuffer(&ibd, &id, out.ib.ReleaseAndGetAddressOf())))
        return false;

    out.indexCount = (uint32_t)m.indices.size();
    out.valid = true;
    return true;
}

static bool rebuildChunkMesh(int cx, int cz) {
    MeshData om, tm;
    if (!buildChunkMeshCPU(cx, cz, true, om)) return false;
    if (!buildChunkMeshCPU(cx, cz, false, tm)) return false;

    lock_guard<mutex> lk(g_meshMutex);
    uint64_t key = chunkKey(cx, cz);
    ActiveChunk& ac = g_activeMeshes[key];
    uploadMeshToGPU(om, ac.opaque);
    uploadMeshToGPU(tm, ac.trans);
    return true;
}

static void releaseChunkMesh(int cx, int cz) {
    lock_guard<mutex> lk(g_meshMutex);
    g_activeMeshes.erase(chunkKey(cx, cz));
}

static void enqueueMeshJob(int cx, int cz, bool priority);   // 前向声明：实现在下方后台线程块

static void markMeshDirty(int cx, int cz) {
    enqueueMeshJob(cx, cz, false);
}

static void markChunksDirtyAround(int wx, int wy, int wz) {
    (void)wy;
    int cx = toChunk(wx), cz = toChunk(wz);
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            shared_ptr<G::ChunkData> cd;
            {
                lock_guard<mutex> lk(G::chunksMutex);
                auto it = G::chunks.find(chunkKey(cx + dx, cz + dz));
                if (it == G::chunks.end()) continue;
                cd = it->second;
            }
            cd->lightDirty = true;   // 只标记，不阻塞主线程
        }
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++)
            enqueueMeshJob(cx + dx, cz + dz, true);   // 队头优先：编辑的区块毫秒级重建
}

// ============ 后台线程：生成+建网格在后台跑，主线程只做 GPU 上传 ============
struct UploadJob { int cx, cz; MeshData om, tm; };

static mutex g_jobMutex;
static condition_variable g_jobCV;
struct GenJob { int cx, cz; bool stub; };
static deque<GenJob> g_genJobs;

static deque<pair<int, int>> g_meshJobs;
static unordered_set<uint64_t> g_genQueued;
static unordered_set<uint64_t> g_meshQueued;
static mutex g_uploadMutex;
static vector<UploadJob> g_uploadJobs;
static vector<thread> g_workers;
static atomic<bool> g_workersRun{ false };

static bool loadOrGenerateChunk(int cx, int cz, bool stub = false); // 前向声明：实现在 Section 11


static void computeColTop(G::ChunkData& cd) {
    cd.colTop.resize(CHUNK_AREA);
    for (int lx = 0; lx < CHUNK_SIZE; lx++)
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int y = RY1 - 1;
            while (y > RY0 && cd.blocks[chunkBlockIndex(lx, y, lz)] == (uint8_t)Block::AIR) y--;
            cd.colTop[chunkBiomeIndex(lx, lz)] = (uint16_t)(y - RY0);
        }
}

static void enqueueMeshJob(int cx, int cz, bool priority) {
    bool isStub = false;
    {
        lock_guard<mutex> lk(G::chunksMutex);
        auto it = G::chunks.find(chunkKey(cx, cz));
        if (it == G::chunks.end()) return;
        isStub = (it->second->lod == 1);
    }
    // ★ 远处 LOD 桩不建网格:桩只负责远处地平线与 GI,建网格纯属浪费几十 GB 显存
    if (isStub) {
        int pcx = toChunk((int)floorf(G::camPos.x));
        int pcz = toChunk((int)floorf(G::camPos.z));
        int ddx = abs(cx - pcx), ddz = abs(cz - pcz);
        if ((ddx > ddz ? ddx : ddz) > STUB_MESH_RADIUS) return;
    }
    lock_guard<mutex> lk(g_jobMutex);
    uint64_t k = chunkKey(cx, cz);
    if (g_meshQueued.count(k)) return;
    g_meshQueued.insert(k);
    if (priority) g_meshJobs.push_front({ cx, cz });
    else          g_meshJobs.push_back({ cx, cz });
}

static void purgeJobs(int cx, int cz) {
    uint64_t k = chunkKey(cx, cz);
    lock_guard<mutex> lk(g_jobMutex);
    g_genQueued.erase(k);
    g_meshQueued.erase(k);
    for (auto it = g_genJobs.begin(); it != g_genJobs.end(); )
        if (chunkKey(it->cx, it->cz) == k) it = g_genJobs.erase(it);
        else ++it;

    for (auto it = g_meshJobs.begin(); it != g_meshJobs.end(); )
        if (chunkKey(it->first, it->second) == k) it = g_meshJobs.erase(it); else ++it;
}

static void runMeshJob(int cx, int cz) {
    for (int dz = -1; dz <= 1; dz++)          // 先重算脏光照(自身+8邻)
        for (int dx = -1; dx <= 1; dx++) {
            shared_ptr<G::ChunkData> cd;
            {
                lock_guard<mutex> lk(G::chunksMutex);
                auto it = G::chunks.find(chunkKey(cx + dx, cz + dz));
                if (it == G::chunks.end()) continue;
                cd = it->second;
            }
            if (cd && cd->lightDirty.exchange(false)) CalculateLightBFS(*cd, cx + dx, cz + dz);
        }
    MeshData om, tm;
    if (!buildChunkMeshCPU(cx, cz, true, om)) return;
    if (!buildChunkMeshCPU(cx, cz, false, tm)) return;
    lock_guard<mutex> lk(g_uploadMutex);
    g_uploadJobs.push_back({ cx, cz, std::move(om), std::move(tm) });
}

// ============ 存盘队列：IO 永不阻塞主线程/生成线程 ============
struct SaveJob { int cx, cz; shared_ptr<G::ChunkData> cd; };
static deque<SaveJob> g_saveJobs;
static mutex g_saveMutex;

static void drainSaves() {          // 退出时同步清空
    for (;;) {
        SaveJob sj;
        {
            lock_guard<mutex> lk(g_saveMutex);
            if (g_saveJobs.empty()) return;
            sj = std::move(g_saveJobs.front());
            g_saveJobs.pop_front();
        }
        Save::g_store.writeChunk(sj.cx, sj.cz, sj.cd->blocks, sj.cd->biomes);
    }
}


static void workerLoop() {
    for (;;) {
        bool gStub = false; // ★ 本轮取出的生成任务是否为 LOD 桩
        SaveJob sj;
        bool haveSave = false;

        {
            lock_guard<mutex> lk(g_saveMutex);
            if (!g_saveJobs.empty()) {
                sj = std::move(g_saveJobs.front());
                g_saveJobs.pop_front();
                haveSave = true;
            }
        }
        if (haveSave) {
            Save::g_store.writeChunk(sj.cx, sj.cz, sj.cd->blocks, sj.cd->biomes);
            continue;
        }

        int gcx = 0, gcz = 0, mcx = 0, mcz = 0;
        bool haveGen = false, haveMesh = false;
        {
            lock_guard<mutex> lk(g_jobMutex);
            if (!g_meshJobs.empty()) {
                auto f = g_meshJobs.front(); g_meshJobs.pop_front();
                g_meshQueued.erase(chunkKey(f.first, f.second));
                mcx = f.first; mcz = f.second; haveMesh = true;
            }
            else if (!g_genJobs.empty()) {
                auto f = g_genJobs.front();
                g_genJobs.pop_front();
                gcx = f.cx;
                gcz = f.cz;
                gStub = f.stub;
                haveGen = true;
            }


        }
        if (haveMesh) {
            bool exists;
            {
                lock_guard<mutex> lk(G::chunksMutex);
                exists = G::chunks.count(chunkKey(mcx, mcz)) != 0;
            }
            if (exists) runMeshJob(mcx, mcz);
            continue;
        }
        if (haveGen) {
            loadOrGenerateChunk(gcx, gcz, gStub);
            {
                lock_guard<mutex> lk(g_jobMutex);
                g_genQueued.erase(chunkKey(gcx, gcz));
            }
            enqueueMeshJob(gcx, gcz, true); // ★ 网格插队:刚生成的块立刻可见


            for (int dz = -1; dz <= 1; dz++)
                for (int dx = -1; dx <= 1; dx++)
                    if (dx || dz) enqueueMeshJob(gcx + dx, gcz + dz, false);
            continue;
        }
        unique_lock<mutex> lk(g_jobMutex);
        g_jobCV.wait_for(lk, chrono::milliseconds(3));
    }
}


static void startWorkers() {
    if (g_workersRun.exchange(true)) return;
    unsigned n = thread::hardware_concurrency();
    n = (n > 8) ? 8 : (n > 1 ? n - 1 : 1); // ★ 桩生成便宜,吞吐提到 8 线程

    for (unsigned i = 0; i < n; i++) g_workers.emplace_back(workerLoop);
    printf("[THREAD] %u workers started\n", n);
}

static void stopWorkers() {
    g_workersRun = false;
    g_jobCV.notify_all();
    for (auto& t : g_workers) if (t.joinable()) t.join();
    g_workers.clear();
}

static void processUploads() {                       // 主线程每帧调用：最多8个GPU上传
    vector<UploadJob> batch;
    {
        lock_guard<mutex> lk(g_uploadMutex);
        if (g_uploadJobs.empty()) return;
        size_t n = min((size_t)8, g_uploadJobs.size());
        batch.reserve(n);
        for (size_t i = g_uploadJobs.size() - n; i < g_uploadJobs.size(); i++)
            batch.push_back(std::move(g_uploadJobs[i]));
        g_uploadJobs.resize(g_uploadJobs.size() - n);
    }
    for (auto& j : batch) {
        bool exists;
        {
            lock_guard<mutex> lk(G::chunksMutex);
            exists = G::chunks.count(chunkKey(j.cx, j.cz)) != 0;
        }
        if (!exists) continue;
        lock_guard<mutex> lk(g_meshMutex);
        ActiveChunk& ac = g_activeMeshes[chunkKey(j.cx, j.cz)];
        uploadMeshToGPU(j.om, ac.opaque);
        uploadMeshToGPU(j.tm, ac.trans);
    }
}











// ============================================================
// Section 9. 着色器
// ============================================================
// ============================================================
// 矩阵约定(宪法,违反即"Y 分量坏了"级惨案):
//   HLSL 一律 mul(v, M) 行向量序;cb 上传 XMMATRIX 原样,禁止 XMMatrixTranspose。
//   GLSL 移植时:gl_Position = M * v 列向量序 + 原样上传 = 同一结果,同样禁止转置。
//   口诀:乘法顺序决定是否转置;row-major 内存 + column-major 读取自带一次转置。
// ============================================================
struct PerFrameCB {

    XMMATRIX world;
    XMMATRIX view;
    XMMATRIX proj;
    XMFLOAT3 sunPos;          float pad1;   // ★ 太阳位置(世界空间);语义锁死,没有任何"方向/反向"歧义
    XMFLOAT3 lightColor;      float pad2;
    XMFLOAT3 ambientColor;    float pad3;
    XMFLOAT3 cameraPos;       float pad4;
    float depthFadeStart;     float pad5;
    float depthFadeEnd;       float pad6;
    XMFLOAT3 cameraRight;     float pad7;
    XMFLOAT3 cameraUp;        float pad8;
    XMFLOAT3 skyZenithColor;  float fogDensity;
    XMFLOAT3 skyHorizonColor; float godRayIntensity;
    float gTime;
    float gPadTime;           // ← 别忘了这个(上轮伤口 4)
    XMFLOAT2 gScreenSizeRcp;
    float pad9;

    // ---- 物理光照扩展段(大气/太阳/环境/曝光) 偏移 356 起 ----
    XMFLOAT3 sunIrradiance;   float sunAngularCos;   // 356: 大气透射后的太阳辐照 + 太阳盘余弦
    XMFLOAT3 ambientSky;      float exposure;       // 372: 天空半球环境光 + 曝光
    XMFLOAT3 ambientGround;   float skyLuminance;   // 388: 地面反弹环境光 + 天空平均亮度
    float fogStart, fogSpan, padE2;   // 原 padE0, padE1, padE2


    // ---- BV 积云(Options.glsl SETTING_CLOUDS_CU_*)----
    XMFLOAT3 cameraForward;  float cloudCoverage;
    XMFLOAT4 cloudParams1;   // x=base y=top z=extinction w=densityMul
    XMFLOAT4 cloudParams2;   // x=windSpeed y=windAngle z=maxDist w=ambBackscatter



};


// ↓↓↓ 粘贴到这里(结构体声明之后、任何使用之前)↓↓↓
static_assert(sizeof(PerFrameCB) == 464, "cb 总大小!");   // 416 + 48(云参数 3×16)

static_assert(offsetof(PerFrameCB, sunPos) == 192, "光照段错位!");
static_assert(offsetof(PerFrameCB, cameraPos) == 240, "相机段错位!");
static_assert(offsetof(PerFrameCB, skyZenithColor) == 304, "天空段错位!");
static_assert(offsetof(PerFrameCB, gTime) == 336, "时间错位!");
static_assert(offsetof(PerFrameCB, gScreenSizeRcp) == 344, "屏幕尺寸错位!");

// ============================================================
// Section 9.1  Physically based atmosphere (ported from Photon
// techniques/atmospherics/air + util/Celestial)
//   Rayleigh / Mie / Ozone coefficients, exponential scale heights,
//   single scattering with analytic optical depth (plane-parallel).
//   Constants are mirrored 1:1 in the HLSL below (ATMO_* block).
// ============================================================
namespace atmo {
    const float RAYLEIGH_H = 8.0f;                       // km
    const float MIE_H = 1.2f;                            // km  (Constants.glsl: MIE_HEIGHT)
    const float BETA_R[3] = { 0.0058f, 0.0135f, 0.0331f };   // 1/km
    const float BETA_M = 0.0040f;                        // 1/km (scatter + absorb)
    const float BETA_O[3] = { 0.00065f, 0.001881f, 0.000085f }; // 1/km
    const float MIE_G = 0.760f;                          // HG anisotropy
    const float SKY_SCALE = 6.2f;                        // radiance -> working space
    const float SUN_ILLUMINANCE = 3.35f;                 // top-of-atmosphere sun irradiance
    const float SKY_TO_IRR = 1.75f;                      // sky radiance -> hemisphere irradiance

    // Pickering 2002 airmass approximation (mu = cos of zenith angle)
    static inline float airmass(float mu) {
        if (mu <= 0.0f) mu = 0.0f;
        return 1.0f / (mu + 0.025f * expf(-11.0f * mu)) * 0.99f;
    }

    struct SkyResult {
        float sunIrr[3];      // direct sun irradiance on a surface facing the sun
        float zenith[3];      // sky radiance at zenith
        float horizon[3];     // sky radiance at horizon (towards sun side averaged)
        float ambSky[3];      // hemisphere irradiance from sky
        float ambGround[3];   // hemisphere irradiance from ground bounce
        float sunTrans[3];    // transmittance towards sun (for tinting)
        float skyLum;
    };

    // dir: unit view direction (world).  sunDir: unit direction towards sun.
    static inline void skyRadiance(const float dir[3], const float sunDir[3],
                                   float muView, float camH, const float transSun[3], float out[3])
    {
        // optical depth towards the view sample (plane-parallel + airmass)
        float am = airmass(muView);
        float ry = expf(-camH / RAYLEIGH_H);
        float my = expf(-camH / MIE_H);
        // phase functions
        float cosT = dir[0] * sunDir[0] + dir[1] * sunDir[1] + dir[2] * sunDir[2];
        float phR = 3.0f / (16.0f * 3.14159265f) * (1.0f + cosT * cosT);
        float g = MIE_G;
        float denom = 1.0f + g * g - 2.0f * g * cosT;
        float phM = (1.0f - g * g) / (4.0f * 3.14159265f * powf(fmaxf(denom, 1e-4f), 1.5f));
        for (int k = 0; k < 3; k++) {
            float tauR = BETA_R[k] * RAYLEIGH_H * ry * am;
            float tauM = BETA_M * MIE_H * my * am * 1.15f;
            float tauO = BETA_O[k] * 18.0f * 0.35f * am;
            float tau = tauR + tauM + tauO;
            float T = expf(-tau);
            // single scattering in-scattering integral (analytic, exponential atmosphere)
            float densR = BETA_R[k] * RAYLEIGH_H * ry;
            float densM = BETA_M * MIE_H * my;
            float ext = fmaxf(densR + densM + 1e-6f, 1e-6f);
            float inscat = (densR * phR + densM * phM) / ext * (1.0f - T);
            out[k] = inscat * transSun[k] * SUN_ILLUMINANCE * SKY_SCALE;
        }
        // ★ 黄昏配色 + 夜空(与 HLSL 完全同源):朝太阳暖橙、背太阳冷蓝,消灭血色天空
        float lum = 0.2126f * out[0] + 0.7152f * out[1] + 0.0722f * out[2];
        if (lum < 0.22f) lum = 0.22f;
        float wAmt = (0.12f - sunDir[1]) * 12.0f;
        if (wAmt < 0.0f) wAmt = 0.0f; if (wAmt > 1.0f) wAmt = 1.0f;
        float sunSide = cosT * 0.5f + 0.5f;
        if (sunSide < 0.0f) sunSide = 0.0f; if (sunSide > 1.0f) sunSide = 1.0f;
        float side2 = sunSide * sunSide * sunSide;
        float dirY = dir[1] < 0.0f ? 0.0f : (dir[1] > 1.0f ? 1.0f : dir[1]);
        float dayAmt = sunDir[1] * 7.0f + 0.50f;
        if (dayAmt < 0.0f) dayAmt = 0.0f; if (dayAmt > 1.0f) dayAmt = 1.0f;
        const float WARM[3] = { 1.00f, 0.80f, 0.60f };
        const float BLUE[3] = { 0.075f, 0.105f, 0.180f };
        const float NS[3]   = { 0.004f, 0.010f, 0.028f };
        for (int k = 0; k < 3; k++) {
            float warm = WARM[k] * lum;
            float blue = BLUE[k] * (0.35f + 0.65f * dirY);
            float dusk = blue + (warm - blue) * side2;
            float v = out[k] + (dusk - out[k]) * wAmt;
            v = v * dayAmt + NS[k] * (0.55f + 0.45f * dirY) * (1.0f - dayAmt);
            out[k] = v < 0.0f ? 0.0f : v;
        }
    }

    // ★ 通用版:太阳方向由昼夜循环给出(不再固定 +X)
    static void solveDir(const float sunDirIn[3], float camH, SkyResult& r) {
        float sunDir[3] = { sunDirIn[0], sunDirIn[1], sunDirIn[2] };
        float invLen = 1.0f / sqrtf(fmaxf(sunDir[0] * sunDir[0] + sunDir[1] * sunDir[1] +
                                          sunDir[2] * sunDir[2], 1e-6f));
        sunDir[0] *= invLen; sunDir[1] *= invLen; sunDir[2] *= invLen;

        // sun transmittance
        float muSun = sunDir[1];
        float amS = airmass(muSun);
        float ry = expf(-camH / RAYLEIGH_H), my = expf(-camH / MIE_H);
        for (int k = 0; k < 3; k++) {
            float tau = (BETA_R[k] * RAYLEIGH_H * ry + BETA_M * MIE_H * my * 1.15f + BETA_O[k] * 6.3f) * amS;
            r.sunTrans[k] = expf(-tau);
            r.sunIrr[k] = SUN_ILLUMINANCE * r.sunTrans[k];
        }
        // slight desaturation so noon sun reads white instead of amber
        float sl = 0.2126f * r.sunIrr[0] + 0.7152f * r.sunIrr[1] + 0.0722f * r.sunIrr[2];
        for (int k = 0; k < 3; k++) r.sunIrr[k] = r.sunIrr[k] * 0.72f + sl * 0.28f;

        // zenith / horizon radiance
        float up[3] = { 0.0f, 1.0f, 0.0f };
        float hor[3] = { sunDir[0], 0.02f, sunDir[2] };
        float hlen = sqrtf(hor[0] * hor[0] + hor[2] * hor[2]); if (hlen > 1e-4f) { hor[0] /= hlen; hor[2] /= hlen; }
        hor[1] = 0.02f;
        skyRadiance(up, sunDir, 1.0f, camH, r.sunTrans, r.zenith);
        skyRadiance(hor, sunDir, 0.02f, camH, r.sunTrans, r.horizon);

        for (int k = 0; k < 3; k++) {
            float avg = 0.45f * r.zenith[k] + 0.55f * r.horizon[k];
            r.ambSky[k] = avg * SKY_TO_IRR;
            r.ambGround[k] = r.ambSky[k] * 0.32f * (0.9f + 0.2f * r.sunTrans[k]);
        }
        r.skyLum = 0.2126f * r.ambSky[0] + 0.7152f * r.ambSky[1] + 0.0722f * r.ambSky[2];
    }

    // 兼容旧调用:太阳固定在 +X 方向
    static void solve(float sunElevY, float camH, SkyResult& r) {
        float sunDir[3] = { 0.70710678f, sunElevY, 0.0f };
        solveDir(sunDir, camH, r);
    }
}



// ★ 粒子着色器
static const char* g_particleShaderSource = R"(
cbuffer PerFrameCB : register(b0) {
    row_major float4x4 gWorld;
    row_major float4x4 gView;
    row_major float4x4 gProj;
    

    float3   gSunPos;          float gPad1;   // ★ 太阳位置(世界空间),不再是方向
    float3   gLightColor;      float gPad2;
    float3   gAmbientColor;    float gPad3;
    float3   gCameraPos;       float gPad4;
    float    gDepthFadeStart;  float gPad5;
    float    gDepthFadeEnd;    float gPad6;
    float3   gCameraRight;     float gPad7;
    float3   gCameraUp;        float gPad8;
    float3   gSkyZenith;      float gFogDensity;        // ← gSkyZenithColor 改回 gSkyZenith(现在这行是 gSkyZenithColor)
    float3   gSkyHorizon;     float gGodRayIntensity;   // ← gSkyHorizonColor 改回 gSkyHorizon

    float    gTime;            float gPadTime;
    float2   gScreenSizeRcp;   float gPad9;

    float3   gSunIrradiance;   float gSunAngularCos;   // 352
    float3   gAmbientSky;      float gExposure;       // 368
    float3   gAmbientGround;   float gSkyLuminance;   // 388
    float    gPadE0;           float gPadE1;  float gPadE2;   // 404 -> 416
};



struct VSIn {
    float3 center : POSITION;    // ★ 就是缺的这个
    float size    : SIZE;
    float2 corner : CORNER;
    float3 color  : COLOR;
    float alpha   : ALPHA;
};

struct VSOut {
    float4 pos   : SV_POSITION;
    float3 color : COLOR;
    float alpha  : ALPHA;
};


VSOut VSParticle(VSIn input) {
    VSOut o;
    float3 worldPos = input.center
        + (gCameraRight * input.corner.x + gCameraUp * input.corner.y) * input.size;
    o.pos   = mul(mul(float4(worldPos, 1.0), gView), gProj);
    o.color = input.color;
    o.alpha = input.alpha;
    return o;
}


float4 PSParticle(VSOut input) : SV_TARGET {
    return float4(input.color, input.alpha);
}
)";

// ============ 天空着色器(模块 A:解析渐变 + 太阳)============
static const char* g_skyShaderSource = R"SKY(
cbuffer PerFrameCB : register(b0) {
    row_major float4x4 gWorld;
    row_major float4x4 gView;
    row_major float4x4 gProj;
    float3   gSunPos;          float gPad1;   // ★ 太阳位置(世界空间),不再是方向
    float3   gLightColor;      float gPad2;
    float3   gAmbientColor;    float gPad3;
    float3   gCameraPos;       float gPad4;
    float    gDepthFadeStart;  float gPad5;
    float    gDepthFadeEnd;    float gPad6;
    float3   gCameraRight;     float gPad7;
    float3   gCameraUp;        float gPad8;
    float3   gSkyZenith;      float gFogDensity;        // ← gSkyZenithColor 改回 gSkyZenith(现在这行是 gSkyZenithColor)
    float3   gSkyHorizon;     float gGodRayIntensity;   // ← gSkyHorizonColor 改回 gSkyHorizon

    float    gTime;            float gPadTime;
    float2   gScreenSizeRcp;   float gPad9;

    float3   gSunIrradiance;   float gSunAngularCos;   // 352
    float3   gAmbientSky;      float gExposure;       // 368
    float3   gAmbientGround;   float gSkyLuminance;   // 388
    float    gPadE0;           float gPadE1;  float gPadE2;   // 404 -> 416
};



struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float cave       : TEXCOORD6;  
};

// ===================== physically based sky =====================
// Physics ported from Photon shaders/techniques/atmospherics/air:
// Rayleigh(8km) / Mie(1.2km, HG g=0.76) / Ozone exponential atmosphere,
// analytic single-scattering optical depth, sun transmittance tinting.
static const float  A_HR = 8.0;
static const float  A_HM = 1.2;
static const float3 A_BR = float3(0.0058, 0.0135, 0.0331);
static const float  A_BM = 0.0022;
static const float3 A_BO = float3(0.00065, 0.001881, 0.000085);
static const float  A_G  = 0.760;
static const float  A_SKY_SCALE = 6.2;
static const float  A_SUN_E = 3.35;

float atmAirmass(float mu) {
    mu = max(mu, 0.0);
    return 0.99 / (mu + 0.025 * exp(-11.0 * mu));
}

// ===== 星空/银河用的廉价哈希与值噪声 =====
float2 hash22(float2 p) {
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return frac(sin(p) * 43758.5453);
}
float hash12(float2 p) { return frac(sin(dot(p, float2(41.3, 289.1))) * 43758.5453); }
float vnoise2(float2 p) {
    float2 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash12(i), b = hash12(i + float2(1, 0));
    float c = hash12(i + float2(0, 1)), d = hash12(i + float2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// radiance of the atmosphere along dir, plus sun disc/halo
float3 skyColor(float3 dir) {
    float3 sunDir = normalize(gSunPos - gCameraPos);   // ★ 指向太阳(由位置推导)
    float  cosT   = dot(dir, sunDir);

    // sun transmittance through the whole atmosphere
    float  amS = atmAirmass(sunDir.y);
    float3 tauSun = (A_BR * A_HR + A_BM * A_HM * 1.15 + A_BO * 6.3) * amS;
    float3 tSun = exp(-tauSun);

    // view ray optical depth
    float  mu  = dir.y;
    float  am  = atmAirmass(max(mu, 0.0));
    float3 tauV = (A_BR * A_HR + A_BM * A_HM * 1.15 + A_BO * 6.3) * am;
    float3 Tv  = exp(-tauV);

    // phase functions: Rayleigh + Henyey-Greenstein (mie)
    float phR = 3.0 / (16.0 * 3.14159265) * (1.0 + cosT * cosT);
    float den = 1.0 + A_G * A_G - 2.0 * A_G * cosT;
    float phM = (1.0 - A_G * A_G) / (4.0 * 3.14159265 * pow(max(den, 1e-4), 1.5));

    float3 densR = A_BR * A_HR;
    float3 densM = A_BM * A_HM;
    float3 ext   = max(densR + densM, 1e-6);
    // g_skyShaderSource 的 skyColor 和 g_shaderSource 的 skyRadiance,两处都是:
// inscat 一行:  * A_SUN_E * A_SKY_SCALE   →  数值 6.2 全部换成 4.2
    // 两处 skyRadiance/skyColor 里的 inscat 行:
    float3 inscat = (densR * phR + densM * phM) / ext * (1.0 - Tv) * tSun * 3.35 * 5.5;
// 多次散射项(它贡献天空的"饱和蓝",上轮砍太狠):
    // 两份 shader 都是:
    float3 ms = (densR + densM) / ext * (1.0 - Tv) * tSun * 3.35 * 5.5 * 0.05;




    float3 sky = inscat + ms;

    // sun disc (physically sized, HDR) + forward mie halo
    // ★ 低太阳角时 tSun 是极端红(R:G:B ≈ 10:1),日盘/日晕直接用就是"血太阳";
    //   这里把日盘颜色收到受控暖橙,同时减弱大范围日晕,黄昏只剩柔和暖光
    float duskAmt = saturate((0.12 - sunDir.y) * 12.0);
    float tSunLum = dot(tSun, float3(0.2126, 0.7152, 0.0722));
    float3 sunTint = lerp(tSun, float3(1.00, 0.80, 0.62) * (tSunLum * 3.0), duskAmt);
    float disc = smoothstep(gSunAngularCos - 0.00006, gSunAngularCos, cosT);
    sky += sunTint * disc * 22.0;                                   // 日盘亮度也压低一档
    sky += sunTint * pow(saturate(cosT), 1800.0) * 2.0;
    sky += sunTint * pow(saturate(cosT), 64.0) * 0.06 * (1.0 - 0.75 * duskAmt);   // 黄昏收掉大范围红晕

    // ★ 黄昏配色 + 夜空(与 main.hlsl skyRadiance 完全同源):朝太阳暖橙、背太阳冷蓝
    float lum = max(dot(sky, float3(0.2126, 0.7152, 0.0722)), 0.0);
    float wAmt = saturate((0.12 - sunDir.y) * 12.0);
    float sunSide = saturate(cosT * 0.5 + 0.5);
    float3 duskWarm = float3(1.00, 0.80, 0.60) * max(lum, 0.22);
    float3 duskBlue = float3(0.075, 0.105, 0.180) * (0.35 + 0.65 * saturate(dir.y));
    float3 duskPal  = lerp(duskBlue, duskWarm, pow(sunSide, 3.0));
    sky = lerp(sky, duskPal, wAmt);

    float dayAmt = saturate(sunDir.y * 7.0 + 0.50);
    float nightF = 1.0 - dayAmt;
    float3 nightSky = float3(0.004, 0.010, 0.028) * (0.55 + 0.45 * saturate(dir.y));
    sky = sky * dayAmt + nightSky * nightF;

    // ===== 星空 + 银河(仅夜间) =====
    if (nightF > 0.02) {
        float3 sd2 = normalize(dir);
        float2 sph = float2(atan2(sd2.x, sd2.z), asin(clamp(sd2.y, -1.0, 1.0)));
        float2 sg = sph * 110.0 + gCameraPos.xz * 0.002f + gTime * 0.01f;
        float2 cell = floor(sg);
        float2 cf = frac(sg) - 0.5;
        float star = 0.0;
        [unroll] for (int k = 0; k < 2; k++) {          // 两层不同密度的星
            float2 c2 = cell + float2(k * 17.0, k * 31.0);
            float2 r = hash22(c2);
            float2 pos = (r - 0.5) * 0.7;
            float d = length(cf - pos);
            float b = hash12(c2 * 1.7);
            star += smoothstep(0.030, 0.0, d) * (0.15 + 0.85 * b * b);
        }
        float tw = 0.75 + 0.25 * sin(gTime * 2.3 + dot(sg, float2(3.1, 1.7)));
        float3 starCol = lerp(float3(0.75, 0.82, 1.0), float3(1.0, 0.92, 0.80), hash12(cell * 3.3));
        sky += starCol * star * tw * nightF * 1.1;

        // 银河:沿一条大圆的微弱亮带 + 噪声质感
        float3 galN = normalize(float3(0.35, 0.28, 0.90));
        float band = exp(-pow(dot(sd2, galN), 2.0) * 10.0);
        float n = vnoise2(sph * 3.0) * 0.6 + vnoise2(sph * 8.0) * 0.4;
        sky += float3(0.55, 0.62, 0.85) * band * (0.35 + 0.65 * n) * nightF * 0.10;
    }

    // 月亮:对冲点的小圆盘(直径 ~2.6°,原实现 ~11° 偏大)+ 柔和光晕
    float moonCos = dot(dir, -sunDir);
    float moonDisc = smoothstep(0.99971, 0.99981, moonCos);
    sky += float3(0.55, 0.62, 0.80) * moonDisc * nightF;
    sky += float3(0.08, 0.10, 0.15) * pow(saturate(moonCos), 120.0) * nightF;


    // below horizon: fade to ground haze so the seam with terrain is invisible
    float below = smoothstep(0.0, -0.06, mu);
    float3 haze = sky * 0.55 + gAmbientGround * 0.25;
    sky = lerp(sky, haze, below);

    return max(sky, 0.0);
}

float4 PSMain(VSOut input) : SV_TARGET {
    float2 ndc = input.uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float tanHalf = tan(radians(37.5));              // FOV 75° 的一半
    float3 fwd = normalize(cross(gCameraRight, gCameraUp));  // 叉积出视线前向
    float aspect = gScreenSizeRcp.y / gScreenSizeRcp.x;      // (1/h)/(1/w) = 宽/高
    float3 dir = normalize(
        gCameraRight * (ndc.x * tanHalf * aspect)
      + gCameraUp    * (ndc.y * tanHalf)
      + fwd);
    return float4(skyColor(dir), 1.0);
}
)SKY";


static const char* g_fullscreenShaderSource = R"(
cbuffer PerFrameCB : register(b0) {
    row_major float4x4 gWorld;
    row_major float4x4 gView;
    row_major float4x4 gProj;
    float3   gSunPos;          float gPad1;   // ★ 太阳位置(世界空间),不再是方向
    float3   gLightColor;      float gPad2;
    float3   gAmbientColor;    float gPad3;
    float3   gCameraPos;       float gPad4;
    float    gDepthFadeStart;  float gPad5;
    float    gDepthFadeEnd;    float gPad6;
    float3   gCameraRight;     float gPad7;
    float3   gCameraUp;        float gPad8;
    float3   gSkyZenith;      float gFogDensity;
    float3   gSkyHorizon;     float gGodRayIntensity;

    float    gTime;            float gPadTime;
    float2   gScreenSizeRcp;   float gPad9;

    float3   gSunIrradiance;   float gSunAngularCos;   // 352
    float3   gAmbientSky;      float gExposure;       // 368
    float3   gAmbientGround;   float gSkyLuminance;   // 388
    float    gPadE0;           float gPadE1;  float gPadE2;   // 404 -> 416
};
Texture2D gScreenTexture : register(t0);
Texture2D gBloomTex : register(t1);
Texture2D gGodTex   : register(t2);
SamplerState gSampler : register(s0);

// 太阳屏幕位置(b1,与阴影共用)
cbuffer ShadowCB : register(b1) {
    row_major float4x4 gSunVP;
    float4 gShadowParams;
    float4 gSunScreen;      // xy=屏幕UV z=可见
};

struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float cave       : TEXCOORD6;  
};
VSOut VSFullscreen(uint id : SV_VertexID) {
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float3 ACESFilm(float3 x) {
    float a = 2.51f; float b = 0.03f; float c = 2.43f;
    float d = 0.59f; float e = 0.14f;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

// ===== Pass 7: God rays(屏幕空间径向模糊) =====
float4 PSGodRays(VSOut input) : SV_TARGET {
    if (gSunScreen.z < 0.5) return float4(0, 0, 0, 1);
    float2 delta = (gSunScreen.xy - input.uv) * 0.85;
    float3 acc = 0;
    float wsum = 0;
    [unroll] for (int i = 1; i <= 24; i++) {
        float t = (float)i / 24.0;
        float3 c = gScreenTexture.SampleLevel(gSampler, input.uv + delta * t, 0).rgb;
        float w = exp(-t * 2.2);
        acc += max(c - 3.0, 0.0) * w;   // 只保留太阳盘附近的高光,避免把云抠成重影
        wsum += w;
    }
    return float4(acc / max(wsum, 1e-3) * max(gGodRayIntensity, 0.0) * 2.0, 1.0);
}

// ===== Pass 9: TAA(历史帧 + 3×3 邻域 clamp 防拖影) =====
Texture2D gHistTex : register(t3);
float4 PSTAA(VSOut input) : SV_TARGET {
    float2 tx = gScreenSizeRcp;
    float3 cur = gScreenTexture.SampleLevel(gSampler, input.uv, 0).rgb;
    float3 mn = cur, mx = cur;
    [unroll] for (int y = -1; y <= 1; y++)
    [unroll] for (int x = -1; x <= 1; x++) {
        float3 s = gScreenTexture.SampleLevel(gSampler, input.uv + float2(x, y) * tx, 0).rgb;
        mn = min(mn, s);
        mx = max(mx, s);
    }
    float3 hist = gHistTex.SampleLevel(gSampler, input.uv, 0).rgb;
    hist = clamp(hist, mn - 0.02, mx + 0.02);   // 防鬼影核心
    return float4(lerp(hist, cur, 0.12), 1.0);
}

// ===== Pass 8: Bloom(阈值 + 黄金角 12-tap 模糊) =====
float4 PSBloom(VSOut input) : SV_TARGET {
    float2 texel = gScreenSizeRcp * 2.0;
    float3 acc = 0;
    [unroll] for (int i = 0; i < 12; i++) {
        float a = (float)i * 2.39996323;
        float r = sqrt(((float)i + 0.5) / 12.0);
        float2 o = float2(cos(a), sin(a)) * r * texel * 6.0;
        float3 c = gScreenTexture.SampleLevel(gSampler, input.uv + o, 0).rgb;
        float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
        float k = saturate((lum - 0.9) / 1.2);
        acc += c * k;
    }
    return float4(acc / 12.0, 1.0);
}

float4 PSFullscreen(VSOut input) : SV_TARGET {
    float3 c = gScreenTexture.Sample(gSampler, input.uv).rgb;
    float3 bloom = gBloomTex.Sample(gSampler, input.uv).rgb;
    float3 god   = gGodTex.Sample(gSampler, input.uv).rgb;
    c += bloom * 0.30 + god;

    // P:昼夜色温(白天暖白、夜晚偏冷蓝)
    {
        float dayAmt = saturate(length(gSunIrradiance) * 0.9f);
        float3 grade = lerp(float3(0.90f, 0.95f, 1.08f), float3(1.04f, 0.99f, 0.93f), dayAmt);
        c *= grade;
    }
    c = max(c, 0.0) * max(gExposure, 0.05f);   // 只读,不赋值

    // ---- filmic tonemap (ACES) ----
    float3 m = ACESFilm(c);
    m = pow(saturate(m), 1.0f / 2.2f);
    float3 s = m * m * (3.0f - 2.0f * m);
    m = lerp(m, s, 0.22f);
    m = saturate(m + 0.012f * (1.0f - m));
    float lum = dot(m, float3(0.2126f, 0.7152f, 0.0722f));
    m = lerp(lum.xxx, m, 1.10f);
    return float4(saturate(m), 1.0f);
}

)";





static const char* g_shaderSource =
R"VJ0(
cbuffer PerFrameCB : register(b0) {
    row_major float4x4 gWorld;
    row_major float4x4 gView;
    row_major float4x4 gProj;
    float3   gSunPos;          float gPad1;   // ★ 太阳位置(世界空间),不再是方向
    float3   gLightColor;      float gPad2;
    float3   gAmbientColor;    float gPad3;
    float3   gCameraPos;       float gPad4;
    float    gDepthFadeStart;  float gPad5;
    float    gDepthFadeEnd;    float gPad6;
    float3   gCameraRight;     float gPad7;
    float3   gCameraUp;        float gPad8;
    float3   gSkyZenith;      float gFogDensity;        // ← gSkyZenithColor 改回 gSkyZenith(现在这行是 gSkyZenithColor)
    float3   gSkyHorizon;     float gGodRayIntensity;   // ← gSkyHorizonColor 改回 gSkyHorizon

    float    gTime;            float gPadTime;
    float2   gScreenSizeRcp;   float gPad9;

    float3   gSunIrradiance;   float gSunAngularCos;   // 352
    float3   gAmbientSky;      float gExposure;        // 368
    float3   gAmbientGround;   float gSkyLuminance;    // 388
    float gFogStart;
    float gFogSpan;
    float gPadE2; // 404 -> 416
    float3 gCameraForward; float gCloudCoverage; // 416
    float4 gCloudParams1;  // 432: base/top/extinction/densityMul
    float4 gCloudParams2;  // 448: windSpeed/windAngle/maxDist/ambBackscatter
}; // -> 464
Texture2DArray gTexArray : register(t0);

SamplerState gSampler : register(s0);
Texture2D giTex : register(t10);
Texture2D shadowTex : register(t11);
SamplerState giSamp : register(s1);      // ← s1,唯一即可

// ================= 太阳阴影深度 pass 资源(b1) =================
cbuffer ShadowCB : register(b1) {
    row_major float4x4 gSunVP;      // 太阳正交投影 viewProj
    float4 gShadowParams;           // x=启用(0/1)  y=texel  z=bias  w=PCF 半径
    float4 gSunScreen;              // xy=屏幕UV z=可见
    float4 gCloudParams;            // xy=中心XZ z=1/跨度 w=强度
};
Texture2D<float> skyShadowDepth : register(t12);
SamplerState shadowSamp : register(s2);
Texture2D<float> gCloudShadowTex : register(t13);
SamplerState cloudShadowSamp : register(s3);
Texture2D g_reflTex : register(t14);                // G4:平面反射(半分辨率,屏幕对齐)
Texture2D<float> g_depthTex : register(t3);          // G:水下深度雾用
#define CAM_NEAR 0.1f
#define CAM_FAR  4096.0f








// ============================================================
// [BV-F1] Fresnel.glsl (bv-packed [9]) 逐行移植
// ============================================================
float3 fresnel_schlick(float cosTheta, float3 f0) { return f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0); }
float  fresnel_schlick(float cosTheta, float  f0) { return f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0); }

// [KUT21] Adobe f82;f82Tint = 1.0 时数学上严格退化为 Schlick(对照表注 1)
float3 fresnel_adobe(float cosTheta, float3 f0, float3 f82Tint) {
    float3 oneMinusF0 = float3(1.0, 1.0, 1.0) - f0;
    float3 b = (oneMinusF0 * 0.462664878484 + f0) * (f82Tint * -17.6513843536 + 17.6513843536);
    return saturate(((cosTheta * cosTheta - cosTheta) * b + oneMinusF0) * pow(1.0 - cosTheta, 5.0) + f0);
}
float fresnel_adobe(float cosTheta, float f0, float f82Tint) {
    float oneMinusF0 = 1.0 - f0;
    float b = (oneMinusF0 * 0.462664878484 + f0) * (f82Tint * -17.6513843536 + 17.6513843536);
    return saturate(((cosTheta * cosTheta - cosTheta) * b + oneMinusF0) * pow(1.0 - cosTheta, 5.0) + f0);
}

// IOR.glsl 不在打包件中 → 标准恒等式重建(全移植中唯一非逐行项)
float ior_f0ToIor(float f0) { float r = sqrt(max(f0, 1e-4)); return (1.0 + r) / (1.0 - r); }

// [BEL25a] 介电透射率(float 版,BV 原样;Hammon 能量守恒用)
float fresnel_dielectricDielectric_transmittance(float cosThetaI, float n1, float n2) {
    float sinThetaI = sqrt(saturate(1.0 - cosThetaI * cosThetaI));
    float sinThetaT = (n1 / n2) * sinThetaI;
    float cosThetaT = sqrt(saturate(1.0 - sinThetaT * sinThetaT));
    float result = 1.0;
    if (sinThetaT < 1.0) {
        float numerator = 2.0 * n1 * cosThetaI;
        float Ts = abs(numerator / (n1 * cosThetaI + n2 * cosThetaT));
        float Tp = abs(numerator / (n1 * cosThetaT + n2 * cosThetaI));
        float beamRatio = abs((n2 * cosThetaT) / (n1 * cosThetaI));
        result = saturate(beamRatio * (Ts * Ts + Tp * Tp) * 0.5);
    }
    return result;
}

// ============================================================
// [BV-F2] BSDF.glsl (bv-packed [8]) 逐行移植
// Material → BvMaterial 最小子集;fma→乘加展开(SM5 无 fma,GLSL 有)
// ============================================================
struct BvMaterial { float f0; float roughness; float3 albedo; };

#define BV_HAMMON_AS_IS 0   // 0=Hammon 论文版(定案:解决发白);1=BV 原文

// [HAM17]
float3 bsdf_diffuseHammon(BvMaterial m, float NDotL, float NDotV, float NDotH, float LDotV) {
    float3 result = float3(0.0, 0.0, 0.0);
    if (NDotL > 0.0) {
        float facing = saturate(0.5 * LDotV + 0.5);
        float fresnelNL = fresnel_dielectricDielectric_transmittance(max(NDotL, 1e-2), 1.0 /*AIR_IOR*/, ior_f0ToIor(m.f0));
        float fresnelNV = fresnel_dielectricDielectric_transmittance(max(NDotV, 1e-2), 1.0 /*AIR_IOR*/, ior_f0ToIor(m.f0));
        float energyConservationFactor = 1.0 - (4.0 * sqrt(m.f0) + 5.0 * m.f0 * m.f0) / 9.0;
        float singleSmooth = (fresnelNL * fresnelNV) / energyConservationFactor;
        float singleRough = facing * (0.9 - 0.4 * facing) * (0.5 + NDotH) / NDotH;
        float single = lerp(singleSmooth, singleRough, m.roughness) * 0.31830988618; // RCP_PI
        float multi = 0.1159 * m.roughness;
#if BV_HAMMON_AS_IS
        result = NDotL * (single + m.albedo * multi);      // ← BV 原文
#else
        result = NDotL * (m.albedo * (single + multi));    // ← Hammon 论文版(可选)
#endif
    }
    return result;
}

// [KAR13] Smith-Schlick 分母
float _bsdf_g_Smith_Schlick_denom(float cosTheta, float k) { return cosTheta * (1.0 - k) + k; }

// [KAR13][HOF10] GGX(D) + Smith-Schlick(V);返回 NDotL * D * V * 0.25(不含 F,BV 约定)
float bsdf_ggx(BvMaterial m, float NDotL, float NDotV, float NDotH) {
    float result = 0.0;
    if (NDotL > 0.0) {
        float NDotH2 = NDotH * NDotH;                       // pow2
        float a2 = m.roughness * m.roughness;               // pow2(roughness)
        float dd = NDotH2 * (a2 - 1.0) + 1.0;
        float d = a2 / max(3.14159265 * dd * dd, 1e-16);
        float k = m.roughness * 0.5;
        float v = 1.0 / (_bsdf_g_Smith_Schlick_denom(NDotL, k) * _bsdf_g_Smith_Schlick_denom(saturate(NDotV), k));
        result = NDotL * d * v * 0.25;
    }
    return result;
}

// ============================================================
// [BV-F5] Rand.glsl IGN (bv-packed [13]) 逐行移植(fract→frac)
// ============================================================
float rand_IGN(float2 v) { return frac(52.9829189 * frac(0.06711056 * v.x + 0.00583715 * v.y)); }
float rand_IGN_f(float2 v, uint frame) {   // 时域版:以后接 TAA 再启用
    frame = frame % 64u;
    v = v + 5.588238 * (float)frame;
    return frac(52.9829189 * frac(0.06711056 * v.x + 0.00583715 * v.y));
}









// PS 末尾:
//float4 gi = giTex.Sample(giSamp, uv);       // uv = 屏幕UV
//float3 irr = lerp(float3(0.12,0.13,0.15), gi.rgb, gi.a);  // 未命中像素回退环境光
//return float4(albedo.rgb * irr, albedo.a);  // 替换你现有的直接光照乘法


struct VSIn {
    float3 pos       : POSITION;
    float3 normal    : NORMAL;
    float3 color     : COLOR;
    float2 uv        : TEXCOORD0;
    float texLayer   : TEXLAYER;
    float shade      : SHADE;
    float light      : LIGHT;
    float cave       : CAVE;      // ★ 就是缺这一行
};


struct VSOut {
    float4 pos      : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 color    : COLOR;
    float2 uv       : TEXCOORD2;
    float  texLayer : TEXCOORD3;
    float  shade    : TEXCOORD4;
    float  light    : TEXCOORD5; // ★ 必须要有这个，用于传递给像素着色器
    float cave       : TEXCOORD6;  
};

VSOut VSMain(VSIn input) {
    VSOut o;
    float4 wp = mul(float4(input.pos, 1.0), gWorld);
    o.worldPos = wp.xyz;
    o.pos = mul(mul(wp, gView), gProj);
    o.normal   = input.normal;
    o.color    = input.color;
    o.uv       = input.uv;
    o.texLayer = input.texLayer;
    o.shade    = input.shade;
    o.light    = input.light;
    o.cave     = input.cave;   // 现在 VSOut 有成员了,这行合法
    return o;
}

// ===== 阴影深度 pass:只输出位置(无 PS,只写深度) =====
struct VSInShadow { float3 pos : POSITION; };
float4 VSMainShadow(VSInShadow input) : SV_POSITION {
    return mul(float4(input.pos, 1.0), gSunVP);
}


float depthFactor(float y) {
    float t = saturate((y - gDepthFadeStart) / (gDepthFadeEnd - gDepthFadeStart));
    t = t * t;
    return 0.30 + 0.70 * t;
}

float3 sampleGI(float4 fragPos) {
    float3 fallback = gAmbientSky * 0.55f;
    // ★ 默认关闭屏幕空间 GI:1/4 分辨率 ReSTIR 在夜间/坡面是"成片黑斑"的来源
    return fallback;
}

// ---- sun shadow map lookup (2048², 4-tap PCF + IGN 旋转抖动) ----
float sampleShadowWorld(float3 wp, float3 N) {
    if (gShadowParams.x < 0.5) return 1.0;
    float4 sp = mul(float4(wp, 1.0), gSunVP);
    float3 ndc = sp.xyz / sp.w;
    float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0) return 1.0;
    // 斜率缩放偏置:掠射角多给偏置(4 倍),彻底压掉自阴影痤疮
    float ndl = saturate(dot(N, normalize(gSunPos - wp)));
    float bias = gShadowParams.z * (1.0 + 4.0 * (1.0 - ndl)) + gShadowParams.y * 1.5;
    // ★ 对称十字 5-tap(固定偏移、不逐像素旋转):避免旋涡状自阴影花纹
    float2 texel = gShadowParams.y * 1.5;
    float s = (skyShadowDepth.SampleLevel(shadowSamp, uv, 0).r < ndc.z - bias) ? 0.0 : 1.0;
    [unroll] for (int i = 0; i < 4; i++) {
        float2 o = float2((i == 0) ? 1.0 : (i == 1) ? -1.0 : 0.0,
                          (i == 2) ? 1.0 : (i == 3) ? -1.0 : 0.0) * texel;
        float d = skyShadowDepth.SampleLevel(shadowSamp, uv + o, 0).r;
        s += (d < ndc.z - bias) ? 0.0 : 1.0;
    }
    return s * 0.2;
}


// ---- shared atmosphere (mirror of the sky shader) ----
static const float  B_HR = 8.0;
static const float  B_HM = 1.2;
static const float3 B_BR = float3(0.0058, 0.0135, 0.0331);
static const float  B_BM = 0.0022;
static const float3 B_BO = float3(0.00065, 0.001881, 0.000085);
static const float  B_G  = 0.760;

float airmassB(float mu) { mu = max(mu, 0.0); return 0.99 / (mu + 0.025 * exp(-11.0 * mu)); }

float3 skyRadiance(float3 dir) {
    float3 sunDir = normalize(gSunPos - gCameraPos);   // ★ 指向太阳(由位置推导)
    float  cosT = dot(dir, sunDir);
    float3 tSun = exp(-(B_BR * B_HR + B_BM * B_HM * 1.15 + B_BO * 6.3) * airmassB(sunDir.y));
    float3 Tv   = exp(-(B_BR * B_HR + B_BM * B_HM * 1.15 + B_BO * 6.3) * airmassB(max(dir.y, 0.0)));
    float phR = 3.0 / (16.0 * 3.14159265) * (1.0 + cosT * cosT);
    float den = 1.0 + B_G * B_G - 2.0 * B_G * cosT;
    float phM = (1.0 - B_G * B_G) / (4.0 * 3.14159265 * pow(max(den, 1e-4), 1.5));
    float3 densR = B_BR * B_HR;
    float3 densM = B_BM * B_HM;
    float3 ext = max(densR + densM, 1e-6);
    float3 s = (densR * phR + densM * phM) / ext * (1.0 - Tv) * tSun * 3.35 * 5.5;
    s += (densR + densM) / ext * tSun * 3.35 * 5.5 * 0.08;

    // ★★ 黄昏/夜空配色(反"血红"):低太阳角时大气单次散射 R≫G≫B(R 是 G/B 的十倍),
    //    直接拿它当雾色/天色就是"血月"。这里做两件事:
    //    1) 去饱和:朝太阳一侧用受控暖橙,背太阳一侧用冷蓝(而不是整片红)
    //    2) 昼夜混合:太阳 -6° 以下基本是夜空
    float lum = max(dot(s, float3(0.2126, 0.7152, 0.0722)), 0.0);
    float wAmt = saturate((0.12 - sunDir.y) * 12.0);          // 低太阳角快速切到调色板
    float sunSide = saturate(cosT * 0.5 + 0.5);              // 朝太阳=1,背太阳=0
    float3 duskWarm = float3(1.00, 0.80, 0.60) * max(lum, 0.22);   // 桃橙(不再是血红)
    float3 duskBlue = float3(0.075, 0.105, 0.180) * (0.35 + 0.65 * saturate(dir.y));
    float3 duskPal  = lerp(duskBlue, duskWarm, pow(sunSide, 3.0)); // 暖色只留在太阳附近
    s = lerp(s, duskPal, wAmt);

    float dayAmt = saturate(sunDir.y * 7.0 + 0.50);
    float3 nightSky = float3(0.004, 0.010, 0.028) * (0.55 + 0.45 * saturate(dir.y));
    s = s * dayAmt + nightSky * (1.0 - dayAmt);

    return max(s, 0.0);
}

)VJ0"
R"VJ1(

float3 applyLight(VSOut input, float3 giIrr) {
    const float INV_PI = 0.31830989;
    const float TERRAIN_GAIN = 1.25f;           // ★ 地形亮度(重光影:直射更强)
    const float CAVE_FALLOFF = 0.02f;           // ★ 只留极轻微洞内衰减(0.22 会把坡地压成黑斑)

    float3 N = normalize(input.normal);
    // ★★ 太阳 = 点光源:每个像素独立由【太阳位置 - 像素位置】求方向,
    //    不存在任何全局方向向量的语义翻车空间("阳面朝东/朝西"根因在此彻底消失)
    float3 toSun = gSunPos - input.worldPos;
    float  d2    = dot(toSun, toSun);
    float3 L     = toSun * rsqrt(max(d2, 1e-4));
    // 柔和距离衰减:太阳距玩家 400 格,衰减常数 640 → 视野内近似均匀,远处/低角度自然变暗
    float  sunAtten = saturate(640.0 * rsqrt(max(d2, 1e-4)));
    float3 V = normalize(gCameraPos - input.worldPos);

    // ---- cave darkening: 地表=1,下探越深越黑(太阳直射除外,它归阴影图管) ----
    float caveDark = exp(-input.cave * CAVE_FALLOFF);

    // ---- albedo ----
    float3 albedo = (input.texLayer >= 0.0)
        ? gTexArray.Sample(gSampler, float3(input.uv, input.texLayer)).rgb
        : input.color;
    float ao = saturate(input.shade);
    albedo *= (0.72f + 0.28f * ao);

    float ndlRaw = dot(N, L);
    float shadow = sampleShadowWorld(input.worldPos, N);
    // ★ 太阳越低阴影越弱(掠射角是自阴影花纹最严重的区间;18 点附近直接淡出)
    {
        float sunHy = normalize(gSunPos - input.worldPos).y;
        shadow = lerp(1.0, shadow, saturate(sunHy * 6.0));
    }
    // ---- 云影:沿太阳方向投影到云层高度取覆盖率 ----
    if (gCloudParams.w > 0.0) {
        float3 sd = normalize(gSunPos - gCameraPos);
        float tCl = (300.0 - input.worldPos.y) / max(sd.y, 0.15);
        float2 cp = input.worldPos.xz + sd.xz * tCl;
        float2 cuv = (cp - gCloudParams.xy) * gCloudParams.z + 0.5;
        if (cuv.x > 0.0 && cuv.x < 1.0 && cuv.y > 0.0 && cuv.y < 1.0) {
            float vis = gCloudShadowTex.SampleLevel(cloudShadowSamp, cuv, 0);
            shadow *= lerp(1.0, vis, gCloudParams.w);
        }
    }
    float ndl = saturate((ndlRaw + 0.18f) / 1.18f);         // wrapped diffuse: no pure-black faces
    ndl = lerp(ndlRaw, ndl, caveDark);                      // ★ 洞内退回严格 N·L,不留假漫反射
    shadow = lerp(shadow, 1.0f, saturate(-ndlRaw * 2.0f) * caveDark); // ★ 背光豁免只在地表生效

    // [BV-F4] 角量一次算齐,喂 BSDF(BV 约定:两个 BSDF 都自带 NDotL 缩放)
    float ndv = saturate(dot(N, V));
    float3 Hv = normalize(L + V);
    float ndh = saturate(dot(N, Hv));
    float ldv = dot(L, V);                              // 注意:LdotV 原始值,函数内部自 saturate
    BvMaterial mat; mat.f0 = 0.04f; mat.roughness = 0.62f; mat.albedo = albedo;
    // ---- direct sun: Hammon(入射分量不含 NDotL,BV 语义);包裹 ndl/cave 豁免保留 ----
    float3 litDirect = bsdf_diffuseHammon(mat, ndl, ndv, ndh, ldv) * (gSunIrradiance * shadow * sunAtten);
    // ---- sky + ground (原样,仍走 Lambert) ----
    float skyUp = saturate(N.y * 0.5f + 0.5f);
    float3 E_sky = lerp(gAmbientGround, gAmbientSky, skyUp);
    float3 litAmbient = albedo * INV_PI * (E_sky * (0.35f + 0.65f * ao) * caveDark);
    // ---- bounced GI (原样) ----
    float3 litGI = albedo * INV_PI * (giIrr * 0.45f * caveDark);
    // ---- block light (原样) ----
    float lv = saturate(input.light);
    float3 litBlock = albedo * INV_PI * (float3(1.00f, 0.78f, 0.52f) * pow(lv, 2.0f) * 0.55f * (0.4f + 0.6f * ao));
    // ★ 夜晚/洞穴:火把光更突出(夜里=火把的舞台)
    {
        float sunY = normalize(gSunPos - gCameraPos).y;
        float nightK = saturate(-sunY * 2.5f + 0.45f);
        litBlock *= (0.85f + 0.65f * nightK);
    }
    // ---- specular: BV GGX(真 Smith-Schlick V 项)+ Adobe Fresnel ----
    float3 spec = fresnel_adobe(saturate(dot(V, Hv)), 0.04f, 1.0f)
                * bsdf_ggx(mat, saturate(ndlRaw), ndv, ndh) * (gSunIrradiance * shadow * sunAtten);
    float3 lit = (litDirect + litAmbient + litGI + litBlock + spec) * TERRAIN_GAIN;

    // G5:水下焦散(y 低于海平面、且有阳光)——程序化动态图案
    if (input.worldPos.y < 63.0f && (gSunIrradiance.r + gSunIrradiance.g) > 0.10f) {
        float2 cp = input.worldPos.xz * 0.9f + gTime * float2(0.13f, 0.09f);
        float ca = sin(cp.x) * sin(cp.y)
                 + 0.5f * sin(cp.x * 1.7f + cp.y * 1.3f + gTime * 0.7f);
        ca = saturate(ca * 0.5f + 0.5f);
        ca = ca * ca * ca;
        float depthK = saturate((63.0f - input.worldPos.y) * 0.05f);
        lit += albedo * INV_PI * gSunIrradiance * ca * 0.50f * depthK;
    }


    // ---- distance fade to sky + 高度雾 + 洞穴雾 ----
    float dist = length(input.worldPos - gCameraPos);
    float t = saturate((dist - gFogStart) / max(gFogSpan, 1.0f));

    float fogMix = t * t;
    // H:高度雾(低洼处更浓、山顶通透)
    float hFog = exp(-max(input.worldPos.y, 0.0f) / 85.0f);
    fogMix = saturate(fogMix + 0.10f * hFog * t);
    float3 viewDir = normalize(input.worldPos - gCameraPos);
    float3 fogColor = skyRadiance(float3(viewDir.x, max(viewDir.y, 0.15f), viewDir.z));
    // N:洞穴氛围 = 冷蓝黑雾 + 额外浓度
    float caveF = saturate(input.cave * 0.03f);
    fogMix = saturate(fogMix + caveF * 0.35f);
    fogColor = lerp(fogColor, float3(0.015f, 0.020f, 0.032f), saturate(input.cave * 0.25f));
    lit = lerp(lit, fogColor, fogMix);

    return lit;
}

float4 PSMain(VSOut input) : SV_TARGET {
    return float4(applyLight(input, sampleGI(input.pos)), 1.0);
}

)VJ1"
R"VJ2(

float4 PSTrans(VSOut input) : SV_TARGET {
    float3 c = applyLight(input, sampleGI(input.pos));
    bool isWaterTop = (input.texLayer < 0.0f && input.normal.y > 0.5f);

    // ===== 活水(露天水面):Gerstner 波 + 水下深度雾 =====
    if (isWaterTop && input.light > 0.90f) {
        // ---- G1: 两层 Gerstner 波(解析法线,波峰尖、波谷圆) ----
        float2 p = input.worldPos.xz;
        float2 d1 = normalize(float2(1.0f, 0.35f));
        float2 d2 = normalize(float2(-0.45f, 1.0f));
        float k1 = 0.32f, k2 = 0.61f, a1 = 0.32f, a2 = 0.15f, w1 = 1.15f, w2 = 1.9f;
        float ph1 = dot(d1, p) * k1 + gTime * w1;
        float ph2 = dot(d2, p) * k2 + gTime * w2;
        float2 g = d1 * (k1 * a1 * cos(ph1)) + d2 * (k2 * a2 * cos(ph2));
        float3 N = normalize(float3(-g.x, 1.0f, -g.y));

        float3 V = normalize(gCameraPos - input.worldPos);
        float3 L = normalize(gSunPos - input.worldPos);   // ★ 水面高光跟随太阳位置
        float ndv = saturate(dot(N, V));
        float fresnel = pow(1.0f - ndv, 3.0f);
        float3 R = reflect(-V, N);
        float3 skyRefl = skyRadiance(R);                  // 与天空同源,反射无缝

        // 波峰反射天空、波谷透出水体色
        float3 waterBody = c * 0.70f + float3(0.020f, 0.070f, 0.120f);
        c = lerp(waterBody, skyRefl, saturate(0.10f + 0.62f * fresnel));

        // 太阳高光(Gerstner 法线,比原来的 sin 叠加更锐)
        float sun = pow(saturate(dot(R, L)), 220.0f);
        c += gSunIrradiance * sun * 0.35f;
        c += gAmbientSky * saturate(sin(ph1) * sin(ph2)) * 0.03f;

        // ---- G3: 水下深度雾(用场景深度算水层厚度) ----
        float dWater = mul(float4(input.worldPos, 1.0), gView).z;
        float sceneD = g_depthTex.Load(int3(int2(input.pos.xy), 0)).r;
        float sceneZ = (CAM_NEAR * CAM_FAR) / (CAM_FAR - sceneD * (CAM_FAR - CAM_NEAR));
        float thick = saturate(sceneZ - dWater);
        float fogW = 1.0f - exp(-thick * 0.05f);
        c = lerp(c, float3(0.030f, 0.110f, 0.160f), fogW * 0.85f);
        return float4(c, 0.72f);
    }

    // ===== 死水(洞穴水/阴影水):镜面感更强 =====
    if (isWaterTop) {
        float3 V = normalize(gCameraPos - input.worldPos);
        float3 R = reflect(-V, float3(0.0f, 1.0f, 0.0f));
        c = lerp(c, skyRadiance(R) * 0.9f + gAmbientSky * 0.2f, 0.25f);
        return float4(c, 0.85f);
    }
    return float4(c, 0.7);
}
)VJ2"
R"VJ3(


// ============ BV 积云 pass(常量名 = Options.glsl)============
// (g_depthTex / CAM_NEAR / CAM_FAR 已由主着色器段声明,此处不再重复)

#define CLOUD_CU_COVERAGE   0.5   // SETTING_CLOUDS_CU_COVERAGE
#define CLOUD_EXTINCTION    0.10  // σ/格
#define CLOUD_STEP_MIN      16    // 远处少步(淡出区,看不出差别)
#define CLOUD_STEP_MAX      48    // 近处 48 步,与 128 步视觉无差
#define CLOUD_LIGHT_STEPS   3     // 向日透射 3 步(银边保留)

#define PI                  3.14159265

float hash13(float3 p) {
    p = frac(p * 0.3183099 + float3(0.1, 0.2, 0.3));
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float vnoise3(float3 p) {
    float3 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(lerp(hash13(i),               hash13(i+float3(1,0,0)), f.x),
                     lerp(hash13(i+float3(0,1,0)), hash13(i+float3(1,1,0)), f.x), f.y),
                lerp(lerp(hash13(i+float3(0,0,1)), hash13(i+float3(1,0,1)), f.x),
                     lerp(hash13(i+float3(0,1,1)), hash13(i+float3(1,1,1)), f.x), f.y), f.z);
}
// fbm: 4 octave, octaves 间距 2.03(与 Bliss densityAtPos 的 2D-3D 噪声结构等价的解析近似)
float fbm4(float3 p) {
    float a = 0.5, s = 0.0, n = 0.0;
    [unroll]
    for (int k = 0; k < 4; k++) { s += vnoise3(p) * a; n += a; a *= 0.5; p = p * 2.03 + 17.3; }
    return s / n;
}

// 密度场:CU_COVERAGE 阈值 + 垂直整形(整形公式 = Photon cumulus.glsl 的
// "egg-shape carve + 底部削薄",常量对应 LOW_TOP_CURVE 48 / BOTTOM_CURVE 128)
float cloudDensityBV(float3 p, out float hOut) {
    float h = saturate((p.y - gCloudParams1.x) / max(gCloudParams1.y - gCloudParams1.x, 1.0));
    hOut = h;
    if (h <= 0.0 || h >= 1.0) return 0.0;
    float3 q = p * 0.015;
    // 风:CU_WIND_SPEED(平移)
    q.xz += gTime * gCloudParams2.x * float2(cos(gCloudParams2.y), sin(gCloudParams2.y));
    q.y += gTime * 0.006;   // ★ 形态演化:噪声域沿 y 缓慢滑动 → 云团翻滚变形(不影响位置)

    float f = fbm4(q);
    float d = smoothstep(1.0 - CLOUD_CU_COVERAGE, 1.0 - CLOUD_CU_COVERAGE * 0.55, f);
    // Photon altitude shaping(意译,常量对齐 LOW_TOP/BOTTOM_CURVE):
    d -= smoothstep(0.2, 1.0, h) * 0.45;    // carve egg: 顶圆(随高度削形)
    d *= smoothstep(0.0, 0.22, h);          // 底部削薄
    return max(d, 0.0) * gCloudParams1.w;   // CU_DENSITY
}

// 双叶 HG,PHASE_RATIO 混合(前向叶为主,β=0.9)
// [BV-F6] PhaseFunc.glsl (bv-packed [11]) — HG / Draine / 近似 Mie [JEN23]
float phasefunc_HenyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    float numerator = 1.0 - g2;
    float denominator = pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return (1.0 / (4.0 * PI)) * numerator / denominator;
}
// [DRA03]
float phasefunc_Draine(float cosTheta, float g, float alpha) {
    float g2 = g * g;
    float denominator1 = pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    float numerator2 = 1.0 + alpha * cosTheta * cosTheta;
    float denominator2 = 1.0 + alpha * (1.0 + 2.0 * g2) / 3.0;
    return (1.0 / (4.0 * PI)) * (1.0 - g2) / denominator1 * numerator2 / denominator2;
}
// [JEN23] NVIDIA approximate Mie;d = 云滴直径(µm)
float phasefunc_HGDraine(float cosTheta, float d) {
    float gHG = exp(-0.0990567 / (d - 1.67154));
    float gD  = exp(-2.20679 / (d + 3.91029) - 0.428934);
    float a   = exp(3.62489 - 8.29288 / (d + 5.52825));
    float wD  = exp(-0.599085 / (d - 0.641583) - 0.665888);
    return lerp(phasefunc_HenyeyGreenstein(cosTheta, gHG), phasefunc_Draine(cosTheta, gD, a), wD);
}
// 保持旧 ×4π 约定,PSMainCloud 一行不用改
float phaseBV(float mu) { return phasefunc_HGDraine(mu, 10.0) * 4.0 * PI; }


// 向日透射:指数步进密度积分 → Beer
float lightTrans(float3 p, float3 sunDir) {
    float sh = 0.0, ls = 8.0;
    float3 lp = p + sunDir * ls * 0.5;
    float hh;
    [unroll]
    for (int k = 0; k < CLOUD_LIGHT_STEPS; k++) {
        sh += cloudDensityBV(lp, hh) * ls;
        lp += sunDir * ls;
        ls *= 1.8;
    }
    return exp(-sh * CLOUD_EXTINCTION);
}

float4 PSMainCloud(float4 pos : SV_Position) : SV_Target {
    float depth = g_depthTex.Load(int3(int2(pos.xy), 0)).r;
    // [0,1] 深度→视距,闭合式(精确对应 zn=0.1 / zf=2000)
    // 天空像素(深度=1)不设远近裁剪——云要一直画到地平线;
    // 地形像素用真实深度做遮挡
    float sceneZ = (depth >= 1.0) ? 1e9
                 : (CAM_NEAR * CAM_FAR) / (CAM_FAR - depth * (CAM_FAR - CAM_NEAR));


    float2 ndc = pos.xy * gScreenSizeRcp * 2.0 - 1.0;
    ndc.y = -ndc.y;   // ★ SV_Position 的 y 向下、NDC 的 y 向上,必须翻转(与天空 shader 一致)
    float3 rd = normalize(gCameraForward
        + gCameraRight * (ndc.x / gProj._11)
        + gCameraUp    * (ndc.y / gProj._22));

    // ★ 深度还原出的是"视轴深度",而 raymarch 的 t 是"沿射线距离":
    //   画面边缘两者相差很大,不换算会导致云错误地盖在地形上(糊脸)/被提前剔除。
    if (sceneZ < 1e8) sceneZ /= max(dot(rd, gCameraForward), 0.05);

    float3 ro = gCameraPos;
    float3 sunDir = normalize(gSunPos - gCameraPos);   // ★ 指向太阳(由位置推导)

    // 板层 [base, top] 求交
    // ★ 相机位于云层内部时直接不画:否则会在镜头前生成一圈雾壳(就是"贴脸的云")
    if (ro.y > gCloudParams1.x && ro.y < gCloudParams1.y) return 0;

    float t0, t1;
    if (abs(rd.y) < 1e-4) {
        if (ro.y < gCloudParams1.x || ro.y > gCloudParams1.y) return 0;
        t0 = 0.0; t1 = gCloudParams2.z;
    } else {
        float ta = (gCloudParams1.x - ro.y) / rd.y;
        float tb = (gCloudParams1.y - ro.y) / rd.y;
        t0 = min(ta, tb); t1 = max(ta, tb);
    }
    t0 = max(t0, 0.0);
    if (t0 > sceneZ || t0 > gCloudParams2.z) return 0;   // 地形挡云 / 太远
    t1 = min(min(t1, sceneZ), gCloudParams2.z);
    if (t1 <= t0) return 0;


    float mu = dot(rd, sunDir);
    float phase = phaseBV(mu);
    float msStr = 0.40;                                          // CU_ISOTROPIC_MS_INTENSITY=1.0 残差近似
    float ambBoost = 1.0 + gCloudParams2.w * saturate(-mu);      // AMB_BACKSCATTER_FACTOR=0.5

    // 步数自适应: 近→STEP_MAX, 远→STEP_MIN(对齐 BV LOW_STEP_MIN/MAX)
    int steps = (int)lerp((float)CLOUD_STEP_MIN, (float)CLOUD_STEP_MAX,
                          saturate(1.0 - t0 / gCloudParams2.z));
    float dt = (t1 - t0) / (float)steps;
    float jitter = hash13(float3(pos.xy, floor(gTime * 60.0)));  // ★ 时域抖动(静态常量=脏渍,时域=胶片颗粒)
    float3 p = ro + rd * (t0 + dt * jitter);

    float trans = 1.0;
    float3 scatter = 0.0;
    [loop]
    for (int s = 0; s < steps; s++) {
        float h;
        float d = cloudDensityBV(p, h);
        if (d > 0.005) {
            float lightT = lightTrans(p, sunDir);
            float3 lum = gSunIrradiance * phase * lightT
                       + gSunIrradiance * msStr * (1.0 - lightT)     // MS 残差(各向同性回散)
                       + lerp(gAmbientGround, gAmbientSky, h) * 0.6 * ambBoost;
            float aStep = 1.0 - exp(-CLOUD_EXTINCTION * d * dt);
            scatter += trans * lum * aStep;
            trans *= exp(-CLOUD_EXTINCTION * d * dt);
            if (trans < 0.02) break;
        }
        p += rd * dt;
    }

    float cov = 1.0 - trans;
    float fade = saturate((gCloudParams2.z - t0) / (gCloudParams2.z * 0.40));
    float3 cloudCol = scatter / max(cov, 1e-3);
    // 远端:云色向天空 radiance 收敛 → 与地平线雾无缝,空带彻底消失
    cloudCol = lerp(skyRadiance(rd), cloudCol, fade);
    cov *= fade;
    return float4(cloudCol, cov);

}





)VJ3";

// ============================================================
// Section 10. 初始化 D3D
// ============================================================
static bool initTextures(HINSTANCE hInst) {
    struct TexRes { int id; const char* name; };
    static const TexRes resList[TEX_COUNT] = {
        {IDR_STONE,         "stone"},
        {IDR_DEEPSLATE,     "shenbanstone"},
        {IDR_DIRT,          "nitu"},
        {IDR_SAND,          "shazi"},
        {IDR_GRASS_TOP,     "caoshang"},
        {IDR_LOG_TOP,       "mutoushang"},
        {IDR_LOG_SIDE,      "mutouce"},
        {IDR_TUFF,          "tuff"},
        {IDR_CALCITE,       "calcite"},
        {IDR_ANDESITE,      "andesite"},
        {IDR_DIORITE,       "diorite"},
        {IDR_GRANITE,       "granite"},
        {IDR_PODZOL_TOP,    "podzol_top"},
        {IDR_PODZOL_SIDE,   "podzol_side"},
        {IDR_MYCELIUM,      "mycelium"},
        {IDR_MUD,           "mud"},
        {IDR_RED_SAND,      "red_sand"},
        {IDR_GRAVEL,        "gravel"},
        {IDR_CLAY,          "clay"},
        {IDR_SANDSTONE,     "sandstone"},
        {IDR_RED_SANDSTONE, "red_sandstone"},
        {IDR_TERRACOTTA,    "terracotta"},
        {IDR_PLANKS,        "planks"},
        {IDR_COAL_ORE,      "coal_ore"},
        {IDR_IRON_ORE,      "iron_ore"},
        {IDR_COPPER_ORE,    "copper_ore"},
        {IDR_GOLD_ORE,      "gold_ore"},
        {IDR_REDSTONE_ORE,  "redstone_ore"},
        {IDR_LAPIS_ORE,     "lapis_ore"},
        {IDR_DIAMOND_ORE,   "diamond_ore"},
        {IDR_EMERALD_ORE,   "emerald_ore"},
    };

    ComPtr<IWICImagingFactory> wic;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(wic.GetAddressOf()));
    if (FAILED(hr)) { printf("[TEX] WIC factory failed 0x%08X\n", (unsigned)hr); return false; }

    int texW = 0, texH = 0;
    vector<vector<uint8_t>> texData(TEX_COUNT);

    for (int i = 0; i < TEX_COUNT; i++) {
        HRSRC hrSrc = FindResourceW(hInst, MAKEINTRESOURCEW(resList[i].id), RT_RCDATA);
        if (!hrSrc) { printf("[TEX] FindResource failed: %s\n", resList[i].name); return false; }
        HGLOBAL hg = LoadResource(hInst, hrSrc);
        void* data = LockResource(hg);
        DWORD size = SizeofResource(hInst, hrSrc);
        if (!data || !size) { printf("[TEX] LoadResource failed: %s\n", resList[i].name); return false; }

        ComPtr<IWICStream> stream;
        wic->CreateStream(stream.GetAddressOf());
        stream->InitializeFromMemory((BYTE*)data, size);

        ComPtr<IWICBitmapDecoder> dec;
        wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                     dec.GetAddressOf());
        ComPtr<IWICBitmapFrameDecode> frame;
        dec->GetFrame(0, frame.GetAddressOf());

        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);
        if (texW == 0) { texW = (int)w; texH = (int)h; }
        if ((int)w != texW || (int)h != texH) {
            printf("[TEX] Size mismatch for %s (%ux%u, expected %dx%d)\n",
                   resList[i].name, w, h, texW, texH);
            return false;
        }

        ComPtr<IWICFormatConverter> conv;
        wic->CreateFormatConverter(conv.GetAddressOf());
        conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                         WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

        texData[i].resize((size_t)w * h * 4);
        conv->CopyPixels(nullptr, w * 4, (UINT)texData[i].size(), texData[i].data());
        printf("[TEX] Loaded %-12s %ux%u\n", resList[i].name, w, h);
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = texW;                              // ← 补回这两行
    desc.Height = texH;                              // ←
    desc.MipLevels = 5;                              // 16x16 完整链:16→8→4→2→1
    desc.ArraySize = TEX_COUNT;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    HRESULT hrc = g_device->CreateTexture2D(&desc, nullptr, g_texArray.GetAddressOf());
    if (FAILED(hrc)) { printf("[TEX] CreateTexture2D failed 0x%08X\n", (unsigned)hrc); return false; }
    for (int i = 0; i < TEX_COUNT; i++) {
        g_context->UpdateSubresource(g_texArray.Get(),
            D3D11CalcSubresource(0, i, 5),
            nullptr, texData[i].data(), texW * 4, 0);
    }



    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = (UINT)-1;   // -1 = 全部 mip 级(原来是 1)

    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = TEX_COUNT;
    hrc = g_device->CreateShaderResourceView(g_texArray.Get(), &srvDesc, g_texArraySRV.GetAddressOf());
    g_context->GenerateMips(g_texArraySRV.Get());

    if (FAILED(hrc)) { printf("[TEX] CreateSRV failed 0x%08X\n", (unsigned)hrc); return false; }

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;

    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hrc = g_device->CreateSamplerState(&sd, g_sampler.GetAddressOf());
    if (FAILED(hrc)) { printf("[TEX] CreateSampler failed 0x%08X\n", (unsigned)hrc); return false; }

    printf("[TEX] Texture2DArray created (%d layers, %dx%d)\n", TEX_COUNT, texW, texH);
    return true;
}

static bool initD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = g_width;
    scd.BufferDesc.Height = g_height;
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    D3D_FEATURE_LEVEL obtained;

    ID3D11Device* devRaw = nullptr;
    ID3D11DeviceContext* ctxRaw = nullptr;
    IDXGISwapChain* scRaw = nullptr;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, 2, D3D11_SDK_VERSION,
        &scd, &scRaw, &devRaw, &obtained, &ctxRaw);
    if (FAILED(hr)) {
        printf("[ERR] D3D11CreateDeviceAndSwapChain: 0x%08X\n", (unsigned)hr);
        return false;
    }
    g_device.Attach(devRaw);
    g_context.Attach(ctxRaw);
    g_swapChain.Attach(scRaw);

    ComPtr<ID3D11Texture2D> backBuffer;
    g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
    g_device->CreateRenderTargetView(backBuffer.Get(), nullptr, g_rtv.GetAddressOf());

    bool factoryOK = SUCCEEDED(D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED, g_d2dFactory.GetAddressOf()));
    bool dwriteOK = SUCCEEDED(DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        (IUnknown**)g_dwFactory.GetAddressOf()));
    printf("[HUD] D2D factory: %s  DWrite: %s\n",
           factoryOK ? "OK" : "FAIL", dwriteOK ? "OK" : "FAIL");

    if (factoryOK && dwriteOK) {
        HRESULT hrt = g_dwFactory->CreateTextFormat(
            L"Consolas", nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"en-us",
            g_textFormat.GetAddressOf());
        if (FAILED(hrt)) {
            hrt = g_dwFactory->CreateTextFormat(
                L"Arial", nullptr,
                DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"en-us",
                g_textFormat.GetAddressOf());
        }
        if (SUCCEEDED(hrt) && g_textFormat) {
            g_dwFactory->CreateTextFormat(
                L"Microsoft YaHei", nullptr,
                DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 24.0f, L"zh-cn",
                g_textFormatBottom.GetAddressOf());
            if (g_textFormatBottom) {
                g_textFormatBottom->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                g_textFormatBottom->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }

            ComPtr<IDXGISurface> surface;
            backBuffer->QueryInterface(__uuidof(IDXGISurface), (void**)surface.GetAddressOf());
            if (surface) {
                D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                    D2D1_RENDER_TARGET_TYPE_DEFAULT,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
                ComPtr<ID2D1RenderTarget> testRT;
                HRESULT hr2 = g_d2dFactory->CreateDxgiSurfaceRenderTarget(
                    surface.Get(), &props, testRT.GetAddressOf());
                if (SUCCEEDED(hr2) && testRT) {
                    g_d2dAvailable = true;
                } else {
                    printf("[HUD] D2D RT probe failed: 0x%08X\n", (unsigned)hr2);
                }
            }
        }
    }
    printf("[HUD] D2D available: %s\n", g_d2dAvailable ? "YES" : "NO");

    D3D11_TEXTURE2D_DESC dtd = {};
    dtd.Width = g_width;
    dtd.Height = g_height;
    dtd.MipLevels = 1;
    dtd.ArraySize = 1;
    dtd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dtd.SampleDesc.Count = 1;
    dtd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    dtd.Usage = D3D11_USAGE_DEFAULT;
    g_device->CreateTexture2D(&dtd, nullptr, g_depthTex.GetAddressOf());
    g_device->CreateDepthStencilView(g_depthTex.Get(), nullptr, g_dsv.GetAddressOf());

    D3D11_VIEWPORT vp = {};

    vp.Width = (float)g_width; vp.Height = (float)g_height;
    vp.MinDepth = 0; vp.MaxDepth = 1;
    g_context->RSSetViewports(1, &vp);

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_BACK;
    rd.DepthClipEnable = TRUE;
    g_device->CreateRasterizerState(&rd, g_rasterState.GetAddressOf());

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_LESS;
    g_device->CreateDepthStencilState(&dsd, g_dssOpaque.GetAddressOf());
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    g_device->CreateDepthStencilState(&dsd, g_dssTrans.GetAddressOf());

    // 天空专用深度状态:深度测试开(被地形遮挡的部分不画),深度写入关(不污染深度缓冲)
    D3D11_DEPTH_STENCIL_DESC skyDsd = {};
    skyDsd.DepthEnable = TRUE;
    skyDsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    skyDsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    g_device->CreateDepthStencilState(&skyDsd, g_skyDSS.GetAddressOf());


    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_device->CreateBlendState(&bd, g_blendState.GetAddressOf());

    ID3DBlob* vsBlob = nullptr; ID3DBlob* psBlob = nullptr;
    ID3DBlob* psTransBlob = nullptr; ID3DBlob* err = nullptr;

    auto compile = [&](const char* entry, const char* target, ID3DBlob** out) {
        HRESULT h = D3DCompile(g_shaderSource, strlen(g_shaderSource),
                               nullptr, nullptr, nullptr, entry, target, 0, 0, out, &err);
        if (FAILED(h)) {
            printf("[ERR] Compile %s: %s\n", entry,
                   err ? (char*)err->GetBufferPointer() : "?");
            return false;
        }

        return true;
    };

    if (!compile("VSMain", "vs_5_0", &vsBlob)) return false;
    if (!compile("PSMain", "ps_5_0", &psBlob)) return false;
    if (!compile("PSTrans", "ps_5_0", &psTransBlob)) return false;

    // ---- 改动⑤: BV 积云 PS ----
    ID3DBlob* cloudBlob = nullptr;
    if (!compile("PSMainCloud", "ps_5_0", &cloudBlob)) return false;
    g_device->CreatePixelShader(cloudBlob->GetBufferPointer(), cloudBlob->GetBufferSize(),
        nullptr, g_cloudPS.GetAddressOf());
    cloudBlob->Release();


    ID3DBlob* skyBlob = nullptr;
    if (FAILED(D3DCompile(g_skyShaderSource, strlen(g_skyShaderSource),
        nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &skyBlob, &err))) {
        printf("[ERR] SkyPS: %s\n", err ? (char*)err->GetBufferPointer() : "?");
        return false;
    }
    g_device->CreatePixelShader(skyBlob->GetBufferPointer(), skyBlob->GetBufferSize(), nullptr, g_skyPS.GetAddressOf());
    skyBlob->Release();



    g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                  nullptr, g_vs.GetAddressOf());
    g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                 nullptr, g_ps.GetAddressOf());
    g_device->CreatePixelShader(psTransBlob->GetBufferPointer(), psTransBlob->GetBufferSize(),
                                 nullptr, g_psTrans.GetAddressOf());

    D3D11_INPUT_ELEMENT_DESC layout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXLAYER", 0, DXGI_FORMAT_R32_FLOAT,       0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "SHADE",    0, DXGI_FORMAT_R32_FLOAT,       0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "LIGHT",    0, DXGI_FORMAT_R32_FLOAT,       0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "CAVE",     0, DXGI_FORMAT_R32_FLOAT,       0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    g_device->CreateInputLayout(layout, 8, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), g_inputLayout.GetAddressOf());

    // ================= Pass 0:阴影深度 pass 初始化 =================
    {
        ID3DBlob* shadowVsBlob = nullptr;
        if (!compile("VSMainShadow", "vs_5_0", &shadowVsBlob)) return false;
        g_device->CreateVertexShader(shadowVsBlob->GetBufferPointer(), shadowVsBlob->GetBufferSize(),
                                     nullptr, &g_shadowVS);
        D3D11_INPUT_ELEMENT_DESC slayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        g_device->CreateInputLayout(slayout, 1, shadowVsBlob->GetBufferPointer(),
                                    shadowVsBlob->GetBufferSize(), &g_shadowLayout);
        shadowVsBlob->Release();

        D3D11_TEXTURE2D_DESC sdd = {};
        sdd.Width = SHADOW_MAP_SIZE; sdd.Height = SHADOW_MAP_SIZE;
        sdd.MipLevels = 1; sdd.ArraySize = 1;
        sdd.Format = DXGI_FORMAT_R32_TYPELESS;
        sdd.SampleDesc.Count = 1;
        sdd.Usage = D3D11_USAGE_DEFAULT;
        sdd.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_device->CreateTexture2D(&sdd, nullptr, &g_shadowDepthTex))) return false;

        D3D11_DEPTH_STENCIL_VIEW_DESC sdv = {};
        sdv.Format = DXGI_FORMAT_D32_FLOAT;
        sdv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        g_device->CreateDepthStencilView(g_shadowDepthTex, &sdv, &g_shadowDSV);

        D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        g_device->CreateShaderResourceView(g_shadowDepthTex, &srv, &g_shadowSRVMap);

        D3D11_SAMPLER_DESC ssp = {};
        ssp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        ssp.AddressU = ssp.AddressV = ssp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        ssp.ComparisonFunc = D3D11_COMPARISON_NEVER;
        ssp.MaxLOD = D3D11_FLOAT32_MAX;
        g_device->CreateSamplerState(&ssp, &g_shadowSamp);

        D3D11_BUFFER_DESC sbc = {};
        sbc.Usage = D3D11_USAGE_DYNAMIC;
        sbc.ByteWidth = sizeof(ShadowCB);
        sbc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        sbc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_device->CreateBuffer(&sbc, nullptr, &g_shadowCB);
        printf("[FX] Shadow pass ready (%dx%d)\n", SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);

        // ---- God rays / Bloom 半分辨率 RT ----
        D3D11_TEXTURE2D_DESC pd = {};
        pd.Width = (UINT)(g_width / 2); pd.Height = (UINT)(g_height / 2);
        pd.MipLevels = 1; pd.ArraySize = 1;
        pd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        pd.SampleDesc.Count = 1;
        pd.Usage = D3D11_USAGE_DEFAULT;
        pd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        g_device->CreateTexture2D(&pd, nullptr, &g_godTex);
        g_device->CreateRenderTargetView(g_godTex, nullptr, &g_godRTV);
        g_device->CreateShaderResourceView(g_godTex, nullptr, &g_godSRV);
        g_device->CreateTexture2D(&pd, nullptr, &g_bloomTex);
        g_device->CreateRenderTargetView(g_bloomTex, nullptr, &g_bloomRTV);
        g_device->CreateShaderResourceView(g_bloomTex, nullptr, &g_bloomSRV);

        // ---- 云影 coverage(R8,CPU 每 6 帧重算) ----
        D3D11_TEXTURE2D_DESC cd = {};
        cd.Width = CLOUD_SHADOW_RES; cd.Height = CLOUD_SHADOW_RES;
        cd.MipLevels = 1; cd.ArraySize = 1;
        cd.Format = DXGI_FORMAT_R8_UNORM;
        cd.SampleDesc.Count = 1;
        cd.Usage = D3D11_USAGE_DYNAMIC;
        cd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        cd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_device->CreateTexture2D(&cd, nullptr, &g_cloudShadowTex);
        g_device->CreateShaderResourceView(g_cloudShadowTex, nullptr, &g_cloudShadowSRV);
        D3D11_SAMPLER_DESC csd = {};
        csd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        csd.AddressU = csd.AddressV = csd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        csd.MaxLOD = D3D11_FLOAT32_MAX;
        g_device->CreateSamplerState(&csd, &g_cloudShadowSamp);
        printf("[FX] Cloud shadow map ready (%d)\n", CLOUD_SHADOW_RES);
    }


    vsBlob->Release(); psBlob->Release(); psTransBlob->Release();



    // ===== 初始化后处理管线 =====
    D3D11_TEXTURE2D_DESC offDesc = {};
    offDesc.Width = g_width;
    offDesc.Height = g_height;
    offDesc.MipLevels = 1;
    offDesc.ArraySize = 1;
    offDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR 格式
    offDesc.SampleDesc.Count = 1;
    offDesc.Usage = D3D11_USAGE_DEFAULT;
    offDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    g_device->CreateTexture2D(&offDesc, nullptr, g_offscreenTex.GetAddressOf());
    g_device->CreateRenderTargetView(g_offscreenTex.Get(), nullptr, g_offscreenRTV.GetAddressOf());
    g_device->CreateShaderResourceView(g_offscreenTex.Get(), nullptr, g_offscreenSRV.GetAddressOf());

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = g_width;
    depthDesc.Height = g_height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;                    // ★ 双重身份
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    g_device->CreateTexture2D(&depthDesc, nullptr, g_offscreenDepthTex.GetAddressOf());

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvd = {};
    dsvd.Format = DXGI_FORMAT_D32_FLOAT;
    dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    g_device->CreateDepthStencilView(g_offscreenDepthTex.Get(), &dsvd, g_offscreenDSV.GetAddressOf());

    D3D11_SHADER_RESOURCE_VIEW_DESC dsrvd = {};                     // ★ 必须叫 dsrvd
    dsrvd.Format = DXGI_FORMAT_R32_FLOAT;
    dsrvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    dsrvd.Texture2D.MipLevels = 1;
    g_device->CreateShaderResourceView(g_offscreenDepthTex.Get(), &dsrvd, g_depthSRV.GetAddressOf());

    // ---- G3:深度副本(水面厚度采样用;避免 DSV/SRV 同时绑定) ----
    {
        D3D11_TEXTURE2D_DESC cpd = depthDesc;
        cpd.BindFlags = D3D11_BIND_SHADER_RESOURCE;      // 只做 SRV
        g_device->CreateTexture2D(&cpd, nullptr, &g_depthCopyTex);
        D3D11_SHADER_RESOURCE_VIEW_DESC cdsrv = {};
        cdsrv.Format = DXGI_FORMAT_R32_FLOAT;
        cdsrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        cdsrv.Texture2D.MipLevels = 1;
        g_device->CreateShaderResourceView(g_depthCopyTex, &cdsrv, &g_depthCopySRV);
    }


    // 编译全屏着色器
            // 编译全屏着色器
    ID3DBlob* fsVSB = nullptr;
    ID3DBlob* fsPSB = nullptr;
    ID3DBlob* fsErr = nullptr;
    if (FAILED(D3DCompile(g_fullscreenShaderSource, strlen(g_fullscreenShaderSource),
        nullptr, nullptr, nullptr, "VSFullscreen", "vs_5_0", 0, 0, &fsVSB, &fsErr))) {
        printf("[ERR] VSFullscreen: %s\n", fsErr ? (char*)fsErr->GetBufferPointer() : "?");
        return false;
    }
    if (FAILED(D3DCompile(g_fullscreenShaderSource, strlen(g_fullscreenShaderSource),
        nullptr, nullptr, nullptr, "PSFullscreen", "ps_5_0", 0, 0, &fsPSB, &fsErr))) {
        printf("[ERR] PSFullscreen: %s\n", fsErr ? (char*)fsErr->GetBufferPointer() : "?");
        return false;
    }
    g_device->CreateVertexShader(fsVSB->GetBufferPointer(), fsVSB->GetBufferSize(), nullptr, g_fsVS.GetAddressOf());
    g_device->CreatePixelShader(fsPSB->GetBufferPointer(), fsPSB->GetBufferSize(), nullptr, g_fsPS.GetAddressOf());
    fsVSB->Release();
    fsPSB->Release();

    // ---- TAA PS ----
    {
        ID3DBlob* taaBlob = nullptr;
        if (FAILED(D3DCompile(g_fullscreenShaderSource, strlen(g_fullscreenShaderSource),
            nullptr, nullptr, nullptr, "PSTAA", "ps_5_0", 0, 0, &taaBlob, &fsErr))) {
            printf("[ERR] PSTAA: %s\n", fsErr ? (char*)fsErr->GetBufferPointer() : "?");
            return false;
        }
        g_device->CreatePixelShader(taaBlob->GetBufferPointer(), taaBlob->GetBufferSize(), nullptr, &g_taaPS);
        taaBlob->Release();
    }

    // ---- God rays / Bloom PS(同一全屏 shader 源) ----
    {
        ID3DBlob* grBlob = nullptr; ID3DBlob* blBlob = nullptr;
        if (FAILED(D3DCompile(g_fullscreenShaderSource, strlen(g_fullscreenShaderSource),
            nullptr, nullptr, nullptr, "PSGodRays", "ps_5_0", 0, 0, &grBlob, &fsErr))) {
            printf("[ERR] PSGodRays: %s\n", fsErr ? (char*)fsErr->GetBufferPointer() : "?");
            return false;
        }
        g_device->CreatePixelShader(grBlob->GetBufferPointer(), grBlob->GetBufferSize(), nullptr, &g_godPS);
        grBlob->Release();
        if (FAILED(D3DCompile(g_fullscreenShaderSource, strlen(g_fullscreenShaderSource),
            nullptr, nullptr, nullptr, "PSBloom", "ps_5_0", 0, 0, &blBlob, &fsErr))) {
            printf("[ERR] PSBloom: %s\n", fsErr ? (char*)fsErr->GetBufferPointer() : "?");
            return false;
        }
        g_device->CreatePixelShader(blBlob->GetBufferPointer(), blBlob->GetBufferSize(), nullptr, &g_bloomPS);
        blBlob->Release();
        printf("[FX] God rays + Bloom ready (half res)\n");
    }



    // 创建采样器
    D3D11_SAMPLER_DESC samDesc = {};
    samDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    g_device->CreateSamplerState(&samDesc, g_fsSampler.GetAddressOf());
    // ===================================



    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.ByteWidth = sizeof(PerFrameCB);
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_device->CreateBuffer(&cbd, nullptr, g_cbuffer.GetAddressOf());

    // ★ 创建粒子资源
    {
        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.Usage = D3D11_USAGE_DYNAMIC;
        vbDesc.ByteWidth = MAX_PARTICLES * 4 * sizeof(ParticleVertex);
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_device->CreateBuffer(&vbDesc, nullptr, g_particleVB.GetAddressOf());

        std::vector<uint32_t> idx(MAX_PARTICLES * 6);
        for (int i = 0; i < MAX_PARTICLES; i++) {
            idx[i*6 + 0] = i*4 + 0;
            idx[i*6 + 1] = i*4 + 1;
            idx[i*6 + 2] = i*4 + 2;
            idx[i*6 + 3] = i*4 + 0;
            idx[i*6 + 4] = i*4 + 2;
            idx[i*6 + 5] = i*4 + 3;
        }
        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
        ibDesc.ByteWidth = (UINT)(idx.size() * sizeof(uint32_t));
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ibData = {};
        ibData.pSysMem = idx.data();
        g_device->CreateBuffer(&ibDesc, &ibData, g_particleIB.GetAddressOf());

        ID3DBlob* pvsBlob = nullptr;
        ID3DBlob* ppsBlob = nullptr;
        ID3DBlob* perr = nullptr;

        HRESULT h1 = D3DCompile(g_particleShaderSource, strlen(g_particleShaderSource),
                                nullptr, nullptr, nullptr,
                                "VSParticle", "vs_5_0", 0, 0, &pvsBlob, &perr);
        if (FAILED(h1)) {
            printf("[ERR] Compile VSParticle: %s\n", perr ? (char*)perr->GetBufferPointer() : "?");
            return false;
        }
        HRESULT h2 = D3DCompile(g_particleShaderSource, strlen(g_particleShaderSource),
                                nullptr, nullptr, nullptr,
                                "PSParticle", "ps_5_0", 0, 0, &ppsBlob, &perr);
        if (FAILED(h2)) {
            printf("[ERR] Compile PSParticle: %s\n", perr ? (char*)perr->GetBufferPointer() : "?");
            return false;
        }

        g_device->CreateVertexShader(pvsBlob->GetBufferPointer(), pvsBlob->GetBufferSize(),
                                     nullptr, g_particleVS.GetAddressOf());
        g_device->CreatePixelShader(ppsBlob->GetBufferPointer(), ppsBlob->GetBufferSize(),
                                    nullptr, g_particlePS.GetAddressOf());

        D3D11_INPUT_ELEMENT_DESC pLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "SIZE",     0, DXGI_FORMAT_R32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "CORNER",   0, DXGI_FORMAT_R32G32_FLOAT,    0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "ALPHA",    0, DXGI_FORMAT_R32_FLOAT,       0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        g_device->CreateInputLayout(pLayout, 5, pvsBlob->GetBufferPointer(),
                                    pvsBlob->GetBufferSize(),
                                    g_particleLayout.GetAddressOf());
        pvsBlob->Release(); ppsBlob->Release();

        D3D11_RASTERIZER_DESC rd2 = {};
        rd2.FillMode = D3D11_FILL_SOLID;
        rd2.CullMode = D3D11_CULL_NONE;
        rd2.DepthClipEnable = TRUE;
        g_device->CreateRasterizerState(&rd2, g_rasterNoCull.GetAddressOf());

        printf("[FX] Particle system ready (%d capacity)\n", MAX_PARTICLES);
    }

    if (!initTextures(GetModuleHandle(nullptr))) {
        printf("[TEX] initTextures failed\n");
        return false;
    }
    return true;
}

// ============================================================
// Section 11. 流式管理
// ============================================================
static int g_lastPlayerCx = INT_MIN;
static int g_lastPlayerCz = INT_MIN;
static void delChunkInternal(int cx, int cz); // ★ 前置声明(实现在控制台区)






static void flushDirtyChunk(int cx, int cz) {
    shared_ptr<G::ChunkData> cd;
    {
        lock_guard<mutex> lk(G::chunksMutex);
        auto it = G::chunks.find(chunkKey(cx, cz));
        if (it == G::chunks.end()) return;
        if (!it->second->dirty.exchange(false)) return;
        if (it->second->lod == 1 && !it->second->edited) return; // ★ 未编辑的桩不落盘
        cd = it->second;

    }
    {
        lock_guard<mutex> lk(g_saveMutex);
        g_saveJobs.push_back({ cx, cz, cd });
    }
    g_jobCV.notify_all();
}

static void flushAllDirty() {
    vector<uint64_t> keys;
    {
        lock_guard<mutex> lk(G::chunksMutex);
        for (auto& kv : G::chunks)
            if (kv.second->dirty) keys.push_back(kv.first);
    }
    for (uint64_t k : keys)
        flushDirtyChunk((int)(k >> 32), (int)(k & 0xFFFFFFFF));
}


static void unloadChunk(int cx, int cz) {
    flushDirtyChunk(cx, cz);
    releaseChunkMesh(cx, cz);
    purgeJobs(cx, cz);
    lock_guard<mutex> lk(G::chunksMutex);
    G::chunks.erase(chunkKey(cx, cz));
}

static bool loadOrGenerateChunk(int cx, int cz, bool stub) {
    uint64_t key = chunkKey(cx, cz);
    {
        lock_guard<mutex> lk(G::chunksMutex);
        if (G::chunks.count(key)) return false;
    }
    auto cd = make_shared<G::ChunkData>();
    bool fromDisk = Save::g_store.readChunk(cx, cz, *cd);
    if (!fromDisk) {
        generateChunkData(*cd, cx, cz, stub);
        cd->lod = stub ? 1 : 0;
        if (!stub) cd->dirty = true; // ★ 桩不落盘(按种子可再生)；实块照旧异步落盘
    }

    computeColTop(*cd);          // 列顶表：网格/光照快速路径共用
    CalculateLightBFS(*cd, cx, cz);
    {
        lock_guard<mutex> lk(G::chunksMutex);
        G::chunks[key] = cd;
    }
    return true;
}


static void streamingUpdate() {
    int pcx = toChunk((int)floorf(G::camPos.x));
    int pcz = toChunk((int)floorf(G::camPos.z));
    bool moved = (pcx != g_lastPlayerCx || pcz != g_lastPlayerCz);
    g_lastPlayerCx = pcx;
    g_lastPlayerCz = pcz;

    if (moved) {
        vector<uint64_t> toUnload;
        {
            lock_guard<mutex> lk(G::chunksMutex);
            for (auto& kv : G::chunks) {
                int cx = (int)(kv.first >> 32);
                int cz = (int)(kv.first & 0xFFFFFFFF);
                if (abs(cx - pcx) > g_unloadRadius || abs(cz - pcz) > g_unloadRadius)
                    toUnload.push_back(kv.first);
            }
        }
        for (uint64_t k : toUnload) {
            int cx = (int)(k >> 32);
            int cz = (int)(k & 0xFFFFFFFF);
            unloadChunk(cx, cz);
        }
    }

    // ★ 硬上限:已加载区块超过 MAX_LOADED_CHUNKS 就卸载最远的 —— 内存封顶,不再靠虚拟内存
    {
        size_t nLoaded = 0;
        { lock_guard<mutex> lk(G::chunksMutex); nLoaded = G::chunks.size(); }
        if (nLoaded > (size_t)MAX_LOADED_CHUNKS) {
            vector<pair<long long, uint64_t>> farList;
            {
                lock_guard<mutex> lk(G::chunksMutex);
                farList.reserve(G::chunks.size());
                for (auto& kv : G::chunks) {
                    int cx = (int)(kv.first >> 32), cz = (int)(kv.first & 0xFFFFFFFF);
                    long long dx = (long long)cx - pcx, dz = (long long)cz - pcz;
                    farList.push_back({ dx * dx + dz * dz, kv.first });
                }
            }
            sort(farList.begin(), farList.end(),
                 [](const pair<long long, uint64_t>& a, const pair<long long, uint64_t>& b) {
                     return a.first > b.first;
                 });
            size_t kill = farList.size() - (size_t)MAX_LOADED_CHUNKS;
            for (size_t i = 0; i < kill; i++)
                unloadChunk((int)(farList[i].second >> 32), (int)(farList[i].second & 0xFFFFFFFF));
        }
    }

    // ★ 移动后补建网格:确保可视半径内每个区块都有网格(含刚进入的桩)
    if (moved) {
        for (int dz = -STUB_MESH_RADIUS; dz <= STUB_MESH_RADIUS; dz++)
            for (int dx = -STUB_MESH_RADIUS; dx <= STUB_MESH_RADIUS; dx++) {
                int cx = pcx + dx, cz = pcz + dz;
                uint64_t k = chunkKey(cx, cz);
                bool has = false;
                { lock_guard<mutex> lk(g_meshMutex); has = g_activeMeshes.count(k) != 0; }
                if (!has) enqueueMeshJob(cx, cz, false);
            }
    }

    struct Job { int cx, cz, d2; bool stub; };
    vector<Job> jobs;
    {
        lock_guard<mutex> lk(G::chunksMutex);
        for (int dz = -g_loadRadius; dz <= g_loadRadius; dz++)
            for (int dx = -g_loadRadius; dx <= g_loadRadius; dx++)
            {
                int cx = pcx + dx, cz = pcz + dz;
                if (G::chunks.count(chunkKey(cx, cz))) continue;
                int d2 = dx * dx + dz * dz;
                jobs.push_back({ cx, cz, d2, d2 > FULL_RADIUS * FULL_RADIUS }); // ★ 圈外=桩
            }
    }
    // ★ 靠近升级:已加载的桩进入 FULL_RADIUS → 删除,由下方扫描按种子重新实加载
    {
        vector<pair<int, int>> ups;
        {
            lock_guard<mutex> lk(G::chunksMutex);
            for (auto& kv : G::chunks) {
                int cx = (int)(kv.first >> 32), cz = (int)(kv.first & 0xFFFFFFFF);
                if (kv.second->lod == 1 &&
                    abs(cx - pcx) <= FULL_RADIUS && abs(cz - pcz) <= FULL_RADIUS)
                    ups.push_back({ cx, cz });
            }
        }
        for (auto& u : ups) {
            delChunkInternal(u.first, u.second);   // 内部已 purge 掉旧队列项
            lock_guard<mutex> lk(g_jobMutex);
            uint64_t k = chunkKey(u.first, u.second);
            if (g_genQueued.insert(k).second)      // .second==true 表示此前不在队列
                g_genJobs.push_front({ u.first, u.second, false }); // ★ 插队:立刻实加载
        }

    }

    if (!jobs.empty() || moved)
    {
        {
            if (moved) // ★ 近的先做
                sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.d2 < b.d2; });
            lock_guard<mutex> lk(g_jobMutex);
            for (auto& j : jobs) {
                uint64_t k = chunkKey(j.cx, j.cz);
                if (g_genQueued.count(k)) continue;
                g_genQueued.insert(k);
                g_genJobs.push_back({ j.cx, j.cz, j.stub });
            }
        }

        g_jobCV.notify_all();
    }
    processUploads();
}






// ============================================================
// Section 11.5. 粒子系统
// ============================================================
struct Particle {
    XMFLOAT3 pos;
    XMFLOAT3 vel;
    XMFLOAT3 color;
    float    life;
    float    maxLife;
    float    size;
    bool     active;
};

static Particle g_particles[MAX_PARTICLES] = {};
static int      g_particleCursor = 0;

static void spawnParticle(const XMFLOAT3& pos, const XMFLOAT3& vel,
                          const XMFLOAT3& color, float life, float size) {
    Particle& p = g_particles[g_particleCursor];
    g_particleCursor = (g_particleCursor + 1) % MAX_PARTICLES;
    p.pos = pos;
    p.vel = vel;
    p.color = color;
    p.life = life;
    p.maxLife = life;
    p.size = size;
    p.active = true;
}

static void spawnBlockBreakParticles(int wx, int wy, int wz, Block b) {
    if (b == Block::AIR) return;
    const BlockInfo& bi = g_blockInfo[(int)b];
    float cr = bi.r / 255.0f;
    float cg = bi.g / 255.0f;
    float cb = bi.b / 255.0f;

    uint64_t s = (uint64_t)(wx * 73856093) ^ (uint64_t)(wy * 19349663) ^ (uint64_t)(wz * 83492791);
    Rng rng(s);

    for (int i = 0; i < PARTICLES_PER_BREAK; i++) {
        XMFLOAT3 pos(
            wx + 0.15f + rng.f01() * 0.7f,
            wy + 0.15f + rng.f01() * 0.7f,
            wz + 0.15f + rng.f01() * 0.7f);

        XMFLOAT3 vel(
            (rng.f01() - 0.5f) * 5.0f,
            3.0f + rng.f01() * 3.0f,
            (rng.f01() - 0.5f) * 5.0f);

        float tint = 0.75f + rng.f01() * 0.5f;
        XMFLOAT3 col(cr * tint, cg * tint, cb * tint);

        float life = 0.8f + rng.f01() * 0.6f;
        float size = 0.10f + rng.f01() * 0.06f;

        spawnParticle(pos, vel, col, life, size);
    }
}

static void updateParticles(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle& p = g_particles[i];
        if (!p.active) continue;

        p.vel.y -= PARTICLE_GRAVITY * dt;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        p.pos.z += p.vel.z * dt;

        p.life -= dt;
        if (p.life <= 0.0f) p.active = false;
    }
}

// ============================================================
// Section 12. 物理 & 射线
// ============================================================
static bool collideAABB(const XMFLOAT3& pos) {
    float minX = pos.x - PLAYER_HALF_W;
    float maxX = pos.x + PLAYER_HALF_W;
    float minY = pos.y;
    float maxY = pos.y + PLAYER_HEIGHT;
    float minZ = pos.z - PLAYER_HALF_W;
    float maxZ = pos.z + PLAYER_HALF_W;

    int x0 = (int)floorf(minX);
    int x1 = (int)floorf(maxX - 1e-4f);
    int y0 = (int)floorf(minY);
    int y1 = (int)floorf(maxY - 1e-4f);
    int z0 = (int)floorf(minZ);
    int z1 = (int)floorf(maxZ - 1e-4f);

    for (int y = y0; y <= y1; y++)
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++)
                if (isSolidBlock(getBlock(x, y, z)))
                    return true;
    return false;
}

static bool blockIntersectsPlayer(float bx, float by, float bz) {
    float pminX = G::camPos.x - PLAYER_HALF_W;
    float pmaxX = G::camPos.x + PLAYER_HALF_W;
    float pminY = G::camPos.y;
    float pmaxY = G::camPos.y + PLAYER_HEIGHT;
    float pminZ = G::camPos.z - PLAYER_HALF_W;
    float pmaxZ = G::camPos.z + PLAYER_HALF_W;

    return (pminX < bx + 1 && pmaxX > bx &&
            pminY < by + 1 && pmaxY > by &&
            pminZ < bz + 1 && pmaxZ > bz);
}

static void updateFlyCamera(float dt) {
    float speed = FLY_SPEED * dt;
    XMFLOAT3 fwd(sinf(G::yaw)*cosf(G::pitch), sinf(G::pitch), cosf(G::yaw)*cosf(G::pitch));
    XMFLOAT3 right(cosf(G::yaw), 0, -sinf(G::yaw));

    if (G::keys['W']) { G::camPos.x += fwd.x*speed; G::camPos.y += fwd.y*speed; G::camPos.z += fwd.z*speed; }
    if (G::keys['S']) { G::camPos.x -= fwd.x*speed; G::camPos.y -= fwd.y*speed; G::camPos.z -= fwd.z*speed; }
    if (G::keys['A']) { G::camPos.x -= right.x*speed; G::camPos.z -= right.z*speed; }
    if (G::keys['D']) { G::camPos.x += right.x*speed; G::camPos.z += right.z*speed; }
    if (G::keys['Q']) G::camPos.y -= speed;
    if (G::keys['E']) G::camPos.y += speed;

    G::playerVel = XMFLOAT3(0,0,0);
    G::onGround = false;
    G::inWater = false;
}

static void updateSurvivalCamera(float dt) {
    int fx = (int)floorf(G::camPos.x);
    int fy = (int)floorf(G::camPos.y + 0.1f);
    int fz = (int)floorf(G::camPos.z);
    Block feetBlock = getBlock(fx, fy, fz);
    G::inWater = (feetBlock == Block::WATER);

    float sy = sinf(G::yaw), cy = cosf(G::yaw);
    float fwdX = sy, fwdZ = cy;
    float rightX = cy, rightZ = -sy;

    float mvX = 0, mvZ = 0;
    if (G::keys['W']) { mvX += fwdX; mvZ += fwdZ; }
    if (G::keys['S']) { mvX -= fwdX; mvZ -= fwdZ; }
    if (G::keys['A']) { mvX -= rightX; mvZ -= rightZ; }
    if (G::keys['D']) { mvX += rightX; mvZ += rightZ; }

    bool sprinting = (G::keys[VK_SHIFT] != 0);
    float speed = sprinting ? WALK_SPEED_SPRINT : WALK_SPEED_NORMAL;

    float mvLen = sqrtf(mvX*mvX + mvZ*mvZ);
    if (mvLen > 1e-4f) {
        mvX = mvX / mvLen * speed;
        mvZ = mvZ / mvLen * speed;
    } else {
        mvX = mvZ = 0;
    }

    bool spaceDown = G::keys[VK_SPACE] != 0;

    if (G::inWater) {
        G::onGround = false;
        if (spaceDown) {
            G::playerVel.y = SWIM_UP_SPEED;
        } else {
            G::playerVel.y = SWIM_DOWN_SPEED;
        }
    } else {
        bool overrideY = false;
        float overrideVelY = 0;

        if (G::onGround && spaceDown) {
            overrideY = true;
            overrideVelY = JUMP_VELOCITY;
            G::onGround = false;
        }

        if (overrideY) {
            G::playerVel.y = overrideVelY;
        } else {
            G::playerVel.y -= GRAVITY * dt;
            if (G::playerVel.y < TERMINAL_VELOCITY)
                G::playerVel.y = TERMINAL_VELOCITY;
        }
    }

    const float STEP = 0.2f;

    {
        float dxRemain = mvX * dt;
        while (fabsf(dxRemain) > 1e-6f) {
            float step = dxRemain;
            if (fabsf(step) > STEP) step = (step > 0) ? STEP : -STEP;
            XMFLOAT3 test = G::camPos;
            test.x += step;
            if (collideAABB(test)) { G::playerVel.x = 0; break; }
            G::camPos.x = test.x;
            dxRemain -= step;
        }
    }

    {
        float dzRemain = mvZ * dt;
        while (fabsf(dzRemain) > 1e-6f) {
            float step = dzRemain;
            if (fabsf(step) > STEP) step = (step > 0) ? STEP : -STEP;
            XMFLOAT3 test = G::camPos;
            test.z += step;
            if (collideAABB(test)) { G::playerVel.z = 0; break; }
            G::camPos.z = test.z;
            dzRemain -= step;
        }
    }

    {
        float dyRemain = G::playerVel.y * dt;
        bool hitGroundThisFrame = false;
        while (fabsf(dyRemain) > 1e-6f) {
            float step = dyRemain;
            if (fabsf(step) > STEP) step = (step > 0) ? STEP : -STEP;
            XMFLOAT3 test = G::camPos;
            test.y += step;
            if (collideAABB(test)) {
                if (step < 0) hitGroundThisFrame = true;
                G::playerVel.y = 0;
                break;
            }
            G::camPos.y = test.y;
            dyRemain -= step;
        }
        if (hitGroundThisFrame) G::onGround = true;
    }

    if (G::camPos.y < RY0) {
        G::camPos.y = RY0 + 1;
        G::playerVel.y = 0;
        G::onGround = true;
    }
}

static void switchMode(int newMode) {
    if (newMode == G::playerMode) return;

    if (newMode == 1) {
        G::camPos.y -= PLAYER_EYE_HEIGHT;

        int safety = 0;
        while (collideAABB(G::camPos) && safety++ < 200)
            G::camPos.y += 1.0f;

        G::playerVel = XMFLOAT3(0,0,0);
        G::onGround = false;
        G::inWater = false;
        G::playerMode = 1;
        printf("[MODE] Survival (gravity + collision)\n");
    } else {
        G::camPos.y += PLAYER_EYE_HEIGHT;
        G::playerVel = XMFLOAT3(0,0,0);
        G::onGround = false;
        G::inWater = false;
        G::playerMode = 0;
        printf("[MODE] Spectator (fly)\n");
    }
}

static void updateCamera(float dt) {
    if (G::playerMode == 1) updateSurvivalCamera(dt);
    else                    updateFlyCamera(dt);
}

struct VoxelHit {
    bool  hit = false;
    int   x, y, z;
    int   px, py, pz;
};
static VoxelHit raycastVoxel(const XMFLOAT3& origin, const XMFLOAT3& dir,
                              float maxDist) {
    VoxelHit r;
    int x = (int)floorf(origin.x);
    int y = (int)floorf(origin.y);
    int z = (int)floorf(origin.z);

    int sx = (dir.x > 0) ? 1 : (dir.x < 0 ? -1 : 0);
    int sy = (dir.y > 0) ? 1 : (dir.y < 0 ? -1 : 0);
    int sz = (dir.z > 0) ? 1 : (dir.z < 0 ? -1 : 0);

    const float INF = 1e30f;
    float tMaxX = (sx != 0) ? ((sx > 0 ? (x + 1 - origin.x) : (origin.x - x)) / fabsf(dir.x)) : INF;
    float tMaxY = (sy != 0) ? ((sy > 0 ? (y + 1 - origin.y) : (origin.y - y)) / fabsf(dir.y)) : INF;
    float tMaxZ = (sz != 0) ? ((sz > 0 ? (z + 1 - origin.z) : (origin.z - z)) / fabsf(dir.z)) : INF;
    float tDX = (sx != 0) ? 1.0f / fabsf(dir.x) : INF;
    float tDY = (sy != 0) ? 1.0f / fabsf(dir.y) : INF;
    float tDZ = (sz != 0) ? 1.0f / fabsf(dir.z) : INF;

    int lastX = x, lastY = y, lastZ = z;
    float t = 0.0f;

    while (t <= maxDist) {
        Block b = getBlock(x, y, z);
        if (b != Block::AIR && b != Block::WATER) {
            r.hit = true;
            r.x = x; r.y = y; r.z = z;
            r.px = lastX; r.py = lastY; r.pz = lastZ;
            return r;
        }
        lastX = x; lastY = y; lastZ = z;

        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            x += sx; t = tMaxX; tMaxX += tDX;
        } else if (tMaxY < tMaxZ) {
            y += sy; t = tMaxY; tMaxY += tDY;
        } else {
            z += sz; t = tMaxZ; tMaxZ += tDZ;
        }
    }
    return r;
}

struct FrustumPlane { XMFLOAT3 n; float d; };
static void extractFrustumPlanes(const XMMATRIX& vp, FrustumPlane planes[6]) {
    XMFLOAT4X4 m; XMStoreFloat4x4(&m, vp);

    planes[0] = {{m._14 + m._11, m._24 + m._21, m._34 + m._31}, m._44 + m._41};
    planes[1] = {{m._14 - m._11, m._24 - m._21, m._34 - m._31}, m._44 - m._41};
    planes[2] = {{m._14 + m._12, m._24 + m._22, m._34 + m._32}, m._44 + m._42};
    planes[3] = {{m._14 - m._12, m._24 - m._22, m._34 - m._32}, m._44 - m._42};
    planes[4] = {{m._13, m._23, m._33}, m._43};
    planes[5] = {{m._14 - m._13, m._24 - m._23, m._34 - m._33}, m._44 - m._43};

    for (int i = 0; i < 6; i++) {
        float len = sqrtf(planes[i].n.x*planes[i].n.x +
                          planes[i].n.y*planes[i].n.y +
                          planes[i].n.z*planes[i].n.z);
        if (len > 0) {
            planes[i].n.x /= len; planes[i].n.y /= len;
            planes[i].n.z /= len; planes[i].d /= len;
        }
    }
}
static bool sphereInFrustum(const FrustumPlane planes[6],
                            const XMFLOAT3& c, float r) {
    for (int i = 0; i < 6; i++) {
        float d = planes[i].n.x * c.x + planes[i].n.y * c.y +
                  planes[i].n.z * c.z + planes[i].d;
        if (d < -r) return false;
    }
    return true;
}

// ============ 昼夜循环状态(24 分钟一天) ============
static const float DAY_LENGTH_SEC = 24.0f * 60.0f;   // 一天 1440 秒=24 分钟
static const float SUN_ORBIT_R    = 400.0f;          // 太阳点光绕玩家的半径(格)
static float g_timeOfDay = 8.0f;                     // 0=午夜 6=日出 12=正午 18=日落
static float g_dayFactor = 1.0f;                     // 0=夜 1=昼(平滑)
static XMFLOAT3 g_sunDirWorld = XMFLOAT3(1, 0, 0);   // 指向太阳的单位向量
static XMFLOAT3 g_sunPosWorld = XMFLOAT3(0, 400, 0); // 太阳点光源世界位置(跟随玩家)
static XMFLOAT3 g_moonPosWorld = XMFLOAT3(0, -400, 0);
static float g_sunIrr[3]   = { 1, 1, 1 };            // 本帧太阳辐照(大气衰减后)
static float g_ambSky[3]   = { 0.3f, 0.35f, 0.45f }; // 本帧天光环境
static float g_ambGround[3] = { 0.1f, 0.1f, 0.12f };
static float g_zenith[3]   = { 0.2f, 0.35f, 0.6f };
static float g_horizon[3]  = { 0.5f, 0.6f, 0.75f };
static float g_skyLum = 0.4f;
static float g_autoExposure = 1.0f;   // P:自动曝光(平滑)
static float g_jitUV[2] = { 0.0f, 0.0f };       // TAA 当前帧抖动(UV 单位)
static float g_jitUVPrev[2] = { 0.0f, 0.0f };   // TAA 上一帧抖动

// ============================= 控制台 =============================
static bool                 g_consoleOpen = false;
static std::string          g_cmdBuf;
static std::vector<std::string> g_cmdLog;      // 回显最近 8 条
static HIMC g_oldImc = nullptr;


// ---- 适配点 ①:接到你的 chunk 存储(返回 nullptr 表示未加载)----
static G::ChunkData* findChunkData(int cx, int cz) {
    lock_guard<mutex> lk(G::chunksMutex);
    auto it = G::chunks.find(chunkKey(cx, cz));
    return it == G::chunks.end() ? nullptr : it->second.get();
}


// ---- 世界坐标写方块(跨 chunk 安全;不跨 chunk 时也可直写)----
static bool worldSetBlock(int x, int y, int z, Block b) {
    if (y < RY0 + 1 || y > RY1 - 2) return false;
    int cx = toChunk(x), cz = toChunk(z);
    G::ChunkData* cd = findChunkData(cx, cz);
    if (!cd) return false;
    int lx = x - cx * CHUNK_SIZE, lz = z - cz * CHUNK_SIZE;
    cd->blocks[chunkBlockIndex(lx, y, lz)] = (uint8_t)b;
    cd->dirty = true;

    return true;
}

// ---- 方块名 → Block(别名随意扩)----
static bool parseBlockName(const std::string& s, Block& out) {
    if (s == "AIR" || s == "空气") { out = Block::AIR;    return true; }
    if (s == "STONE" || s == "石头") { out = Block::STONE;  return true; }
    if (s == "DIRT" || s == "泥土") { out = Block::DIRT;   return true; }
    if (s == "GRASS" || s == "草") { out = Block::GRASS;  return true; }
    if (s == "SAND" || s == "沙子") { out = Block::SAND;   return true; }
    if (s == "WATER" || s == "水") { out = Block::WATER;  return true; }
    if (s == "LAVA" || s == "岩浆") { out = Block::LAVA;   return true; }
    if (s == "LOG" || s == "WOOD" || s == "木头") { out = Block::OAK_LOG;   return true; }
    if (s == "LEAVES" || s == "树叶") { out = Block::OAK_LEAVES; return true; }

    return false;
}

// ---- 区块序列号:世界半宽 8192 → 每轴 512 chunk,序列号 = 行×1024 + 列 ----
static const int CHW = CANYON_WORLD_HALF / CHUNK_SIZE;          // 512
static long long chunkSerial(int cx, int cz) {
    return (long long)(cz + CHW) * (CHW * 2) + (cx + CHW);
}

//static long long chunkSerial(int cx, int cz) { ... }      // ← 这行之后
static void releaseChunkMesh(int cx, int cz);
static void purgeJobs(int cx, int cz);
//extern int g_lastPlayerCx;   // 不行,static 变量不能 extern

static void delChunkInternal(int cx, int cz) {           
    {
        lock_guard<mutex> lk(G::chunksMutex);
        G::chunks.erase(chunkKey(cx, cz));
    }
    releaseChunkMesh(cx, cz);
    purgeJobs(cx, cz);
    g_lastPlayerCx = INT_MIN;   // ★ 强制 streamingUpdate 认为位置变了 → 自动重新入队生成
}
//static void execCommand(std::string line) {               // ← 这行之前


static void execCommand(std::string line) {
    // 去首尾空格
    while (!line.empty() && (line.front() == ' ')) line.erase(line.begin());
    while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.pop_back();
    if (line.empty()) return;
    g_cmdLog.push_back(line);
    while (g_cmdLog.size() > 8) g_cmdLog.erase(g_cmdLog.begin());

    if (line.rfind("/tp", 0) == 0) {
        float x, y, z;
        if (sscanf(line.c_str(), "/tp %f %f %f", &x, &y, &z) == 3) {
            G::camPos = XMFLOAT3(x, y, z);
            G::playerVel = XMFLOAT3(0, 0, 0);
            g_cmdLog.push_back("tp ok");

            //g_cmdLog.push_back("tp ok");
        }
        else g_cmdLog.push_back("usage: /tp x y z");
        return;
    }
    if (line.rfind("/fill", 0) == 0) {
        int x, y, z, dx, dy, dz; char bn[32] = { 0 };
        int n = sscanf(line.c_str(), "/fill %d %d %d %d %d %d %31s",
            &x, &y, &z, &dx, &dy, &dz, bn);
        Block b;
        if (n == 7 && parseBlockName(bn, b)) {
            if (dx < 0) { x += dx; dx = -dx; }
            if (dy < 0) { y += dy; dy = -dy; }
            if (dz < 0) { z += dz; dz = -dz; }
            if ((long long)(dx + 1) * (dy + 1) * (dz + 1) > 2000000LL) {
                g_cmdLog.push_back("too big (max 2e6)"); return;
            }
            int ok = 0;
            for (int iy = y; iy <= y + dy; iy++)
                for (int ix = x; ix <= x + dx; ix++)
                    for (int iz = z; iz <= z + dz; iz++)
                        if (worldSetBlock(ix, iy, iz, b)) ok++;
            g_cmdLog.push_back("fill: " + std::to_string(ok) + " blocks");
        }
        else g_cmdLog.push_back("usage: /fill x y z dx dy dz [AIR|STONE|...]");
        return;
    }
    // ---- /time: 昼夜时间(0-24 小时,一天 24 分钟) ----
    if (line.rfind("/time", 0) == 0) {
        float t = 0.0f;
        if (sscanf(line.c_str(), "/time set %f", &t) == 1) {
            if (t < 0.0f) t = 0.0f;
            if (t > 23.99f) t = 23.99f;
            g_timeOfDay = t;
            char buf[96];
            sprintf_s(buf, "time set to %.2f h (sun %s)", t,
                      (t >= 6.0f && t < 18.0f) ? "above horizon" : "below horizon");
            g_cmdLog.push_back(buf);
        } else {
            char buf[96];
            sprintf_s(buf, "time = %.2f h  (0=midnight 6=sunrise 12=noon 18=sunset)", g_timeOfDay);
            g_cmdLog.push_back(buf);
            g_cmdLog.push_back("usage: /time set <0-24>   (24 min per day)");
        }
        return;
    }
    if (line.rfind("/del", 0) == 0) {
        int a, b2;
        if (sscanf(line.c_str(), "/del %d %d", &a, &b2) == 2) {
            delChunkInternal(a, b2);

            g_cmdLog.push_back("del chunk " + std::to_string(a) + "," + std::to_string(b2));
        }
        else {
            long long sn;
            if (sscanf(line.c_str(), "/del %lld", &sn) == 1) {
                int cz = (int)(sn / (CHW * 2)) - CHW;
                int cx = (int)(sn % (CHW * 2)) - CHW;
                delChunkInternal(cx, cz);

                g_cmdLog.push_back("del serial -> " + std::to_string(cx) + "," + std::to_string(cz));
            }
            else g_cmdLog.push_back("usage: /del cx cz  或  /del 序列号");
        }
        return;
    }
    if (line.rfind("/room", 0) == 0) {
        int n;
        if (sscanf(line.c_str(), "/room set %d", &n) == 1) {
            if (n > ROOM_MAX) { n = ROOM_MAX; g_cmdLog.push_back("clamped to 15 (memory cap)"); }
            if (n < 1) n = 1;
            g_loadRadius = n;
            g_unloadRadius = n + 4;
            g_renderDistance = (float)n * 16.0f;
            g_lastPlayerCx = INT_MIN;           // 强制 streamingUpdate 重扫重排
            double estMB = 0.0;
            {   // 内存预估:方形区域 × 0.5MB/块
                long long cnt = (long long)(2 * n + 1) * (2 * n + 1);
                estMB = cnt * 0.5;
            }
            char buf[96];
            sprintf_s(buf, "room=%d chunks (%d blocks), ~%.0f MB RAM", n, n * 16, estMB);
            g_cmdLog.push_back(buf);
            if (estMB > 8000) g_cmdLog.push_back("WARN: >8GB, 生成会排队很久");
        }
        else g_cmdLog.push_back("usage: /room set <1-192>");
        return;
    }
    g_cmdLog.push_back("unknown cmd");
}

static void drawHudAndCrosshair(float fps) {
    if (!g_d2dAvailable || !g_d2dFactory || !g_textFormat) return;

    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                      (void**)backBuffer.GetAddressOf()))) return;

    ComPtr<IDXGISurface> dxgiSurface;
    if (FAILED(backBuffer->QueryInterface(__uuidof(IDXGISurface),
                                          (void**)dxgiSurface.GetAddressOf()))) return;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));

    ComPtr<ID2D1RenderTarget> rt;
    HRESULT hrRT = g_d2dFactory->CreateDxgiSurfaceRenderTarget(
        dxgiSurface.Get(), &props, rt.GetAddressOf());
    if (FAILED(hrRT) || !rt) return;

    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    ComPtr<ID2D1SolidColorBrush> brushText, brushShadow, brushCross;
    rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.95f), brushText.GetAddressOf());
    rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1.00f), brushShadow.GetAddressOf());
    rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1.00f), brushCross.GetAddressOf());

    rt->BeginDraw();

    {
        int pcx = toChunk((int)floorf(G::camPos.x));
        int pcz = toChunk((int)floorf(G::camPos.z));

        size_t numChunks;
        {
            lock_guard<mutex> lk(G::chunksMutex);
            numChunks = G::chunks.size();
        }

        wchar_t buf[512];
        swprintf_s(buf, 512,
            L"FPS: %.1f\n"
            L"X: %.2f   Y: %.2f   Z: %.2f\n"
            L"Chunk: (%d, %d)  serial: %lld\n"
            L"Loaded: %zu\n"
            L"Time: %.2f h   Sun: %s   (/time set 0-24)\n"
            L"Mode: %s",
            fps, G::camPos.x, G::camPos.y, G::camPos.z,
            pcx, pcz, chunkSerial(pcx, pcz),
            numChunks,
            g_timeOfDay, (g_dayFactor > 0.5f) ? L"day" : L"night",
            (G::playerMode == 1) ? L"Survival [M]" : L"Spectator [N]");


        const wchar_t* txt = buf;
        UINT32 len = (UINT32)wcslen(txt);

        if (brushShadow) {
            rt->DrawText(txt, len, g_textFormat.Get(),
                D2D1::RectF(11, 11, 511, 211), brushShadow.Get());
            rt->DrawText(txt, len, g_textFormat.Get(),
                D2D1::RectF(13, 13, 513, 213), brushShadow.Get());
        }
        if (brushText) {
            rt->DrawText(txt, len, g_textFormat.Get(), D2D1::RectF(12, 12, 512, 212), brushText.Get());
        }
    }                                              // ← 第一个块的收尾大括号,插在它之后
    if (g_consoleOpen) {                           // ← 新增从这里开始
        std::wstring wcmd(g_cmdBuf.begin(), g_cmdBuf.end());
        std::wstring line = L"> " + wcmd + L"_";
        rt->DrawText(line.c_str(), (UINT32)line.size(), g_textFormat.Get(),
            D2D1::RectF(12, 230, 700, 260), brushText.Get());
        float ty = 265;
        for (int i = (int)g_cmdLog.size() - 1; i >= 0 && ty < 500; i--, ty += 24) {
            std::wstring wl(g_cmdLog[i].begin(), g_cmdLog[i].end());
            rt->DrawText(wl.c_str(), (UINT32)wl.size(), g_textFormat.Get(),
                D2D1::RectF(12, ty, 900, ty + 24), brushShadow.Get());
        }
    }
    if (brushCross) {                              // ← 原有代码不动

        float cx = g_width  * 0.5f;
        float cy = g_height * 0.5f;
        const float gap   = 3.0f;
        const float len   = 9.0f;
        const float thick = 1.5f;
        const float half  = thick * 0.5f;

        rt->FillRectangle(
            D2D1::RectF(cx - half, cy - gap - len, cx + half, cy - gap),
            brushCross.Get());
        rt->FillRectangle(
            D2D1::RectF(cx - half, cy + gap, cx + half, cy + gap + len),
            brushCross.Get());
        rt->FillRectangle(
            D2D1::RectF(cx - gap - len, cy - half, cx - gap, cy + half),
            brushCross.Get());
        rt->FillRectangle(
            D2D1::RectF(cx + gap, cy - half, cx + gap + len, cy + half),
            brushCross.Get());
    }

    if (g_textFormatBottom) {
        wchar_t buf[128];
        swprintf_s(buf, 128, L"[%d/%d]  %ls",
                   G::heldBlockIndex + 1, g_selectableCount,
                   blockNameCN(G::heldBlock));

        D2D1_RECT_F rb = D2D1::RectF(
            g_width * 0.5f - 300.0f, g_height - 60.0f,
            g_width * 0.5f + 300.0f, g_height - 16.0f);

        if (brushShadow) {
            D2D1_RECT_F rs1 = D2D1::RectF(rb.left + 1, rb.top + 1, rb.right + 1, rb.bottom + 1);
            D2D1_RECT_F rs2 = D2D1::RectF(rb.left + 2, rb.top + 2, rb.right + 2, rb.bottom + 2);
            rt->DrawText(buf, (UINT32)wcslen(buf), g_textFormatBottom.Get(), rs1, brushShadow.Get());
            rt->DrawText(buf, (UINT32)wcslen(buf), g_textFormatBottom.Get(), rs2, brushShadow.Get());
        }
        if (brushText) {
            rt->DrawText(buf, (UINT32)wcslen(buf), g_textFormatBottom.Get(), rb, brushText.Get());
        }
    }

    rt->EndDraw();
}

// ============ 昼夜循环 → 太阳状态(每帧,必须在 giFrame 之前) ============
static void computeSunState(const XMFLOAT3& eyePos) {
    // 角度:6 点=东方地平线,12 点=头顶,18 点=西方地平线(24 点=午夜)
    // +X 定为东方(与旧版天空/大气常量一致),太阳沿 X-Y 大圆运动
    float ang = (g_timeOfDay - 6.0f) / 24.0f * 6.28318530718f;
    float dx = cosf(ang);
    float dy = sinf(ang);
    g_sunDirWorld = XMFLOAT3(dx, dy, 0.0f);

    // 太阳/月亮位置:跟随玩家的点光源(半径 400 格);方向只由几何决定,没有"反向"歧义
    g_sunPosWorld = XMFLOAT3(eyePos.x + g_sunDirWorld.x * SUN_ORBIT_R,
                             eyePos.y + g_sunDirWorld.y * SUN_ORBIT_R,
                             eyePos.z + g_sunDirWorld.z * SUN_ORBIT_R);
    g_moonPosWorld = XMFLOAT3(eyePos.x - g_sunDirWorld.x * SUN_ORBIT_R,
                              eyePos.y - g_sunDirWorld.y * SUN_ORBIT_R,
                              eyePos.z - g_sunDirWorld.z * SUN_ORBIT_R);

    // 昼夜因子:smoothstep(-0.08, 0.12, sunDir.y)
    float t = (dy + 0.08f) / 0.20f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    g_dayFactor = t * t * (3.0f - 2.0f * t);

    // 物理大气:用当前太阳方向解算辐照/天空/环境
    atmo::SkyResult skyRes = {};
    float sd[3] = { g_sunDirWorld.x, g_sunDirWorld.y, g_sunDirWorld.z };
    atmo::solveDir(sd, 0.02f, skyRes);

    // 夕阳染色(大气透射已经偏红,这里只轻微加强橙色,正午几乎无影响)
    float warm = dy * 2.0f;
    if (warm < 0.0f) warm = 0.0f; if (warm > 1.0f) warm = 1.0f;
    float tintG = 0.38f + 0.62f * warm;
    float tintB = 0.25f + 0.75f * warm;

    float night = 1.0f - g_dayFactor;
    const float MOON_AMB[3] = { 0.022f, 0.030f, 0.052f };

    for (int k = 0; k < 3; k++) {
        g_sunIrr[k]    = skyRes.sunIrr[k] * g_dayFactor;
        g_ambSky[k]    = skyRes.ambSky[k] + MOON_AMB[k] * night;
        g_ambGround[k] = skyRes.ambGround[k] + MOON_AMB[k] * 0.6f * night;
        g_zenith[k]    = skyRes.zenith[k] * (0.35f + 0.65f * g_dayFactor) + 0.004f * night;
        g_horizon[k]   = skyRes.horizon[k] * (0.35f + 0.65f * g_dayFactor) + 0.008f * night;
    }
    g_sunIrr[1] *= tintG;
    g_sunIrr[2] *= tintB;
    // 低角度强化:日落/日出时太阳被大气衰减得很厉害,视觉上补一点暖光(MC 手感)
    float lowBoost = 1.0f + 0.8f * (1.0f - warm);   // 夕阳暖光增益收敛
    g_sunIrr[0] *= lowBoost;
    g_sunIrr[1] *= lowBoost;
    g_sunIrr[2] *= lowBoost;
    g_skyLum = 0.2126f * g_ambSky[0] + 0.7152f * g_ambSky[1] + 0.0722f * g_ambSky[2];

    // ★ GI(CPU ReSTIR)用同一份太阳/天光:阴面再也不可能"吃错方向的太阳"
    cigi::setSunLight(g_sunDirWorld.x, g_sunDirWorld.y, g_sunDirWorld.z, g_sunIrr, g_ambSky);
}

static void renderFrame(float fps) {
    (void)fps;


    //cb.cameraFwd = dir;   // 你已有的 dir 变量(sinf/pitch 那个)

    // ---- ① 探测是否在水中(最先,后面都要用)----
    XMFLOAT3 eyePosPre = G::camPos;
    if (G::playerMode == 1) eyePosPre.y += PLAYER_EYE_HEIGHT;
    bool underwater = (getBlock((int)floorf(eyePosPre.x),
        (int)floorf(eyePosPre.y),
        (int)floorf(eyePosPre.z)) == Block::WATER);

    // ---- ①.5 昼夜循环:先算太阳状态,GI 和 cb 都吃这一份 ----
    computeSunState(eyePosPre);



    // ---- ② 清屏色 ----
    float clearColor[4] = { g_modSettings.skyColor[0], g_modSettings.skyColor[1],
                            g_modSettings.skyColor[2], 1.0f };
    if (underwater) { clearColor[0] = 0.03f; clearColor[1] = 0.12f; clearColor[2] = 0.28f; }





    // 恢复深度状态,方块 pass 原样开始
    g_context->OMSetDepthStencilState(g_dssOpaque.Get(), 0);








    // 3. cb 填写 + Map/Unmap 上传
    // 4. PSSetConstantBuffers / PSSetShaderResources / PSSetSamplers

    // 5. 天空 pass(见下方深度写入修正)
    // 6. 不透明方块循环 DrawIndexed
    // 7. 透明方块循环
    // 8. 粒子
    // 9. 解绑 → 后备缓冲 → 后处理 → HUD → Present




    // 3. 基础渲染状态
    g_context->RSSetState(g_rasterState.Get());
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)g_width; vp.Height = (float)g_height;
    vp.MinDepth = 0; vp.MaxDepth = 1;
    g_context->RSSetViewports(1, &vp);

    XMFLOAT3 eyePos = G::camPos;
    // ★ 摄像机是否在水中
    //bool underwater = false;
    //{
        //XMFLOAT3 ep = eyePos;
        //if (G::playerMode == 1) ep.y += PLAYER_EYE_HEIGHT;
        //underwater = (getBlock((int)floorf(ep.x), (int)floorf(ep.y), (int)floorf(ep.z)) == Block::WATER);
    //}

    if (G::playerMode == 1) eyePos.y += PLAYER_EYE_HEIGHT;

    XMVECTOR eye = XMLoadFloat3(&eyePos);
    XMFLOAT3 dir(sinf(G::yaw)*cosf(G::pitch), sinf(G::pitch),
                 cosf(G::yaw)*cosf(G::pitch));
    XMVECTOR dirV = XMLoadFloat3(&dir);
    XMMATRIX view = XMMatrixLookToLH(eye, dirV, XMVectorSet(0,1,0,0));
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(75.0f), (float)g_width / g_height, 0.1f, 4096.0f);

    // ★ L:TAA 亚像素抖动(Halton 8 点)
    {
        static const float HALTON[8][2] = {
            { 0.500000f, 0.333333f }, { 0.250000f, 0.666667f },
            { 0.750000f, 0.111111f }, { 0.125000f, 0.444444f },
            { 0.625000f, 0.777778f }, { 0.375000f, 0.222222f },
            { 0.875000f, 0.555556f }, { 0.062500f, 0.888889f }
        };
        static int s_jit = 0;
        int ji = s_jit++ & 7;
        proj.r[2].m128_f32[0] += (HALTON[ji][0] - 0.5f) * 2.0f / (float)g_width;
        proj.r[2].m128_f32[1] += (HALTON[ji][1] - 0.5f) * 2.0f / (float)g_height;
    }


    XMMATRIX vpMat = view * proj;
    FrustumPlane planes[6];
    extractFrustumPlanes(vpMat, planes);
    giFrame(vpMat, eyePos);                            // ★ CPU ReSTIR GI 每帧


    PerFrameCB cb = {};      // 所有未显式赋值的字段保证为 0,杜绝 NaN 垃圾

    cb.world = XMMatrixIdentity();
    cb.view = view;
    cb.proj = proj;

    // ---- ④ 时间 uniform(必须在这,cb 已存在)----
    static const auto t0 = chrono::steady_clock::now();
    cb.gTime = chrono::duration<float>(chrono::steady_clock::now() - t0).count();
    cb.gScreenSizeRcp = XMFLOAT2(1.0f / (float)g_width, 1.0f / (float)g_height);


    // ================= 昼夜循环 + 太阳点光源 =================
    // 状态已在帧首由 computeSunState() 算好:这里只做搬运。
    // 语义锁定:gSunPos 是【太阳在世界空间的位置】,不是方向;
    //          片元着色器用 gSunPos-worldPos 逐像素求 L,任何"反向"歧义都不复存在。
    cb.sunPos        = g_sunPosWorld;                  // ★ 太阳位置(点光源)
    cb.sunIrradiance = XMFLOAT3(g_sunIrr[0], g_sunIrr[1], g_sunIrr[2]);
    cb.lightColor    = cb.sunIrradiance;               // 兼容旧字段
    cb.ambientColor  = XMFLOAT3(g_ambSky[0], g_ambSky[1], g_ambSky[2]);
    cb.sunAngularCos = 0.99985f;                       // 太阳盘面(~2° 直径,稍微放大)
    cb.ambientSky    = XMFLOAT3(g_ambSky[0], g_ambSky[1], g_ambSky[2]);
    cb.ambientGround = XMFLOAT3(g_ambGround[0], g_ambGround[1], g_ambGround[2]);
    cb.exposure      = g_autoExposure;
    cb.skyLuminance  = g_skyLum;
    cb.skyZenithColor  = XMFLOAT3(g_zenith[0], g_zenith[1], g_zenith[2]);
    cb.skyHorizonColor = XMFLOAT3(g_horizon[0], g_horizon[1], g_horizon[2]);

    cb.cameraPos = eyePos;
    cb.depthFadeStart = -50.0f;
    cb.depthFadeEnd   =  60.0f;

    XMFLOAT3 camRight(cosf(G::yaw), 0.0f, -sinf(G::yaw));
    XMVECTOR upV = XMVector3Cross(dirV, XMLoadFloat3(&camRight));
    XMFLOAT3 camUp;
    XMStoreFloat3(&camUp, XMVector3Normalize(upV));

    cb.cameraRight = camRight;
    cb.cameraUp = camUp;
    cb.cameraForward = dir;
    // ---- BV: SETTING_CLOUDS_CU_* ----
    cb.cloudCoverage = 0.5f;                         // SETTING_CLOUDS_CU_COVERAGE
    cb.cloudParams1 = XMFLOAT4(
        300.0f,  // 云底(抬到出生高度之上,视差更自然)
        316.0f,  // 云顶

        0.10f,                                       // 消光 σ/格(CU_DENSITY 折算)
        1.0f);                                       // SETTING_CLOUDS_CU_DENSITY
    cb.cloudParams2 = XMFLOAT4(
        0.020f,             // ★ 风速:单位是"噪声域/秒",×(1/0.015)≈世界 1.3 格/秒,缓慢西风
        0.9f,               // ★ 风向角(弧度):≈52°,自东向西偏南漂(想正西用 0.0)
        4000.0f,            // 最远步进距离
        0.5f);              // AMB_BACKSCATTER_FACTOR


    if (underwater) {

        cb.skyZenithColor = XMFLOAT3(0.03f, 0.11f, 0.22f);
        cb.skyHorizonColor = XMFLOAT3(0.02f, 0.07f, 0.16f);
        cb.fogDensity = 0.0f;
        cb.godRayIntensity = 0.0f;
        cb.ambientSky = XMFLOAT3(0.10f, 0.26f, 0.40f);
        cb.ambientGround = XMFLOAT3(0.04f, 0.12f, 0.22f);
        cb.exposure = 1.15f;
        // ★ 水下:收边近而陡,配合短清屏色,水面下没有"远处地形"概念
        cb.fogStart = 60.0f;
        cb.fogSpan = 40.0f;
    }
    else {
        cb.skyZenithColor = XMFLOAT3(g_zenith[0], g_zenith[1], g_zenith[2]);
        cb.skyHorizonColor = XMFLOAT3(g_horizon[0], g_horizon[1], g_horizon[2]);
        cb.fogDensity = (g_modSettings.loaded && g_modSettings.fogEnable) ? 0.0020f : 0.0012f;
        cb.godRayIntensity = 0.0f;   // ★ 默认关闭 God rays(白天那层"糊脸"的径向光雾)
        // ★ 露天:坡道随 /room set 的渲染距离外推;白天雾更淡(0.72 起,到边界才全浓)
        cb.fogStart = g_renderDistance * 0.88f;   // 雾从 88% 距离起(更通透)
        cb.fogSpan = g_renderDistance * 0.12f;    // 到边界才全浓
        if (!g_dbgFog) { cb.fogStart = 1e9f; cb.fogSpan = 1.0f; }   // F10:关雾

    }



    D3D11_MAPPED_SUBRESOURCE mapped;
    g_context->Map(g_cbuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &cb, sizeof(cb));
    g_context->Unmap(g_cbuffer.Get(), 0);

    // ================= Pass 0:太阳阴影深度(从太阳视角渲一遍深度) =================
    {
        UINT sstride = sizeof(Vertex), soffset = 0;
        float bf0[4] = { 0,0,0,0 };
        bool sunUp = (g_sunDirWorld.y > -0.05f) && (g_dayFactor > 0.02f);
        ShadowCB scb = {};
        if (sunUp && g_shadowDSV && g_shadowCB && g_shadowVS && g_shadowLayout) {
            XMFLOAT3 target(eyePos.x, eyePos.y, eyePos.z);
            XMFLOAT3 up(0.0f, 1.0f, 0.0f);
            XMFLOAT3 ts(g_sunPosWorld.x - target.x,
                        g_sunPosWorld.y - target.y,
                        g_sunPosWorld.z - target.z);
            float tl = sqrtf(ts.x*ts.x + ts.y*ts.y + ts.z*ts.z);
            if (tl > 1e-4f) { ts.x /= tl; ts.y /= tl; ts.z /= tl; }
            if (fabsf(ts.y) > 0.98f) up = XMFLOAT3(0.0f, 0.0f, 1.0f);   // 太阳接近天顶时换 up
            XMMATRIX sunView = XMMatrixLookAtLH(XMLoadFloat3(&g_sunPosWorld), XMLoadFloat3(&target), XMLoadFloat3(&up));
            XMMATRIX sunProj = XMMatrixOrthographicOffCenterLH(-SHADOW_RANGE, SHADOW_RANGE,
                                                               -SHADOW_RANGE, SHADOW_RANGE,
                                                               1.0f, 1200.0f);
            scb.sunVP = sunView * sunProj;
            scb.params = XMFLOAT4(1.0f, 1.0f / (float)SHADOW_MAP_SIZE, 0.0015f, 2.2f);

            // 太阳屏幕位置(供 God rays 径向模糊使用)
            {
                XMFLOAT4 spc;
                XMStoreFloat4(&spc, XMVector4Transform(
                    XMVectorSet(g_sunPosWorld.x, g_sunPosWorld.y, g_sunPosWorld.z, 1.0f), vpMat));
                float su = 0.5f, sv = 0.5f, svis = 0.0f;
                if (spc.w > 1e-4f) {
                    float nx = spc.x / spc.w, ny = spc.y / spc.w, nz = spc.z / spc.w;
                    su = nx * 0.5f + 0.5f;
                    sv = 0.5f - ny * 0.5f;
                    if (nx >= -1.2f && nx <= 1.2f && ny >= -1.2f && ny <= 1.2f &&
                        nz >= 0.0f && nz <= 1.0f && g_sunDirWorld.y > -0.02f)
                        svis = 1.0f;
                }
                scb.sunScreen = XMFLOAT4(su, sv, svis, 0.0f);
            }

            ID3D11RenderTargetView* nullRtv[1] = { nullptr };
            g_context->OMSetRenderTargets(1, nullRtv, g_shadowDSV);
            g_context->ClearDepthStencilView(g_shadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
            D3D11_VIEWPORT svp = { 0.0f, 0.0f, (float)SHADOW_MAP_SIZE, (float)SHADOW_MAP_SIZE, 0.0f, 1.0f };
            g_context->RSSetViewports(1, &svp);
            g_context->IASetInputLayout(g_shadowLayout);
            g_context->VSSetShader(g_shadowVS, nullptr, 0);
            g_context->PSSetShader(nullptr, nullptr, 0);
            g_context->OMSetBlendState(nullptr, bf0, 0xFFFFFFFF);
            g_context->OMSetDepthStencilState(g_dssOpaque.Get(), 0);
            ID3D11Buffer* sbc[1] = { g_shadowCB };
            g_context->VSSetConstantBuffers(1, 1, sbc);
            {
                lock_guard<mutex> lk(g_meshMutex);
                for (auto& kv : g_activeMeshes) {
                    ChunkMesh& m = kv.second.opaque;
                    if (!m.valid) continue;
                    g_context->IASetVertexBuffers(0, 1, m.vb.GetAddressOf(), &sstride, &soffset);
                    g_context->IASetIndexBuffer(m.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
                    g_context->DrawIndexed(m.indexCount, 0, 0);
                }
            }
        } else {
            scb.sunVP = XMMatrixIdentity();
            scb.params = XMFLOAT4(0.0f, 1.0f / (float)SHADOW_MAP_SIZE, 0.0015f, 1.6f);
            scb.sunScreen = XMFLOAT4(0.5f, 0.5f, 0.0f, 0.0f);
        }
        if (g_shadowCB) {
            // ---- 云影:每 6 帧重算一次覆盖率贴图(云在缓慢移动) ----
            int pcx = toChunk((int)floorf(eyePos.x));
            int pcz = toChunk((int)floorf(eyePos.z));
            scb.cloudParams = XMFLOAT4((float)(pcx * 16 + 8), (float)(pcz * 16 + 8),
                                       1.0f / CLOUD_SHADOW_SPAN, 0.0f);   // ★ 云影关闭(黑斑/抖动主因)
            static int s_cf = 0;
            s_cf++;
            if (g_cloudShadowTex) {
                static Noise2D s_cloudNoise(0xC10D5EEDu);
                float tt = cb.gTime;
                float wxo = tt * 0.02f / 0.015f * cosf(0.9f);   // 与云风速/风向一致
                float wzo = tt * 0.02f / 0.015f * sinf(0.9f);
                D3D11_MAPPED_SUBRESOURCE cm;
                if (SUCCEEDED(g_context->Map(g_cloudShadowTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &cm))) {
                    float cx0 = (float)(pcx * 16 + 8) - CLOUD_SHADOW_SPAN * 0.5f;
                    float cz0 = (float)(pcz * 16 + 8) - CLOUD_SHADOW_SPAN * 0.5f;
                    float step = CLOUD_SHADOW_SPAN / (float)CLOUD_SHADOW_RES;
                    for (int y = 0; y < CLOUD_SHADOW_RES; y++) {
                        unsigned char* row = (unsigned char*)cm.pData + (size_t)y * cm.RowPitch;
                        float wzC = cz0 + (y + 0.5f) * step + wzo;
                        for (int x = 0; x < CLOUD_SHADOW_RES; x++) {
                            float wxC = cx0 + (x + 0.5f) * step + wxo;
                            float f = s_cloudNoise.fbm(wxC * 0.015f, wzC * 0.015f, 3);
                            float cov = (f - 0.5f) / 0.275f;          // CLOUD_CU_COVERAGE=0.5
                            cov = cov < 0.0f ? 0.0f : (cov > 1.0f ? 1.0f : cov);
                            cov = cov * cov * (3.0f - 2.0f * cov);
                            row[x] = (unsigned char)((1.0f - cov * 0.55f) * 255.0f);
                        }
                    }
                    g_context->Unmap(g_cloudShadowTex, 0);
                }
            }

            if (!g_dbgShadow) scb.params.x = 0.0f;   // F9:临时关阴影
            D3D11_MAPPED_SUBRESOURCE smp;
            if (SUCCEEDED(g_context->Map(g_shadowCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &smp))) {
                memcpy(smp.pData, &scb, sizeof(scb));
                g_context->Unmap(g_shadowCB, 0);
            }
        }
        // 恢复屏幕视口(主 pass 随后会重设 RT)
        D3D11_VIEWPORT vpm = {};
        vpm.Width = (float)g_width; vpm.Height = (float)g_height;
        vpm.MinDepth = 0; vpm.MaxDepth = 1;
        g_context->RSSetViewports(1, &vpm);
    }

    // ================= Pass 6:水面平面反射(半分辨率) =================
    static const bool PLANAR_REFL_ENABLE = false;
    if (PLANAR_REFL_ENABLE && g_reflRTV && g_reflDSV && g_reflTex) {
        UINT rstride = sizeof(Vertex), roffset = 0;
        float bfr[4] = { 0,0,0,0 };
        // 镜像相机:绕 y = 水面高度 的平面(手写反射矩阵,避免依赖 XMMatrixReflection)
        const float waterY = 63.0f;
        XMMATRIX reflM = XMMatrixIdentity();
        reflM.r[1] = XMVectorSet(0.0f, -1.0f, 0.0f, 2.0f * waterY);   // y' = 2w - y
        XMMATRIX viewRefl = reflM * view;
        {
            PerFrameCB cbr = cb;
            cbr.view = viewRefl;
            D3D11_MAPPED_SUBRESOURCE m3;
            if (SUCCEEDED(g_context->Map(g_cbuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m3))) {
                memcpy(m3.pData, &cbr, sizeof(cbr));
                g_context->Unmap(g_cbuffer.Get(), 0);
            }
        }
        ID3D11RenderTargetView* rrtv[1] = { g_reflRTV };
        g_context->OMSetRenderTargets(1, rrtv, g_reflDSV);
        float rclear[4] = { g_ambSky[0] * 0.5f, g_ambSky[1] * 0.55f, g_ambSky[2] * 0.65f, 1.0f };
        g_context->ClearRenderTargetView(g_reflRTV, rclear);
        g_context->ClearDepthStencilView(g_reflDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
        D3D11_VIEWPORT rvp = { 0.0f, 0.0f, (float)(g_width / 2), (float)(g_height / 2), 0.0f, 1.0f };
        g_context->RSSetViewports(1, &rvp);
        g_context->RSSetState(g_rasterNoCull.Get());
        g_context->IASetInputLayout(g_inputLayout.Get());
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_vs.Get(), nullptr, 0);
        g_context->PSSetShader(g_ps.Get(), nullptr, 0);
        g_context->OMSetDepthStencilState(g_dssOpaque.Get(), 0);
        g_context->OMSetBlendState(nullptr, bfr, 0xFFFFFFFF);
        ID3D11Buffer* cbr0[] = { g_cbuffer.Get() };
        g_context->VSSetConstantBuffers(0, 1, cbr0);
        g_context->PSSetConstantBuffers(0, 1, cbr0);
        if (g_shadowCB) {
            ID3D11Buffer* sb1[] = { g_shadowCB };
            g_context->VSSetConstantBuffers(1, 1, sb1);
            g_context->PSSetConstantBuffers(1, 1, sb1);
        }
        if (g_texArraySRV) g_context->PSSetShaderResources(0, 1, g_texArraySRV.GetAddressOf());
        if (g_giSRV)       g_context->PSSetShaderResources(10, 1, &g_giSRV);
        if (g_shadowSRVMap) g_context->PSSetShaderResources(12, 1, &g_shadowSRVMap);
        if (g_cloudShadowSRV) g_context->PSSetShaderResources(13, 1, &g_cloudShadowSRV);
        ID3D11SamplerState* rsamp[] = { g_sampler.Get() };
        g_context->PSSetSamplers(0, 1, rsamp);
        if (g_shadowSamp) g_context->PSSetSamplers(2, 1, &g_shadowSamp);
        if (g_cloudShadowSamp) g_context->PSSetSamplers(3, 1, &g_cloudShadowSamp);
        {
            lock_guard<mutex> lk(g_meshMutex);
            for (auto& kv : g_activeMeshes) {
                ChunkMesh& m = kv.second.opaque;
                if (!m.valid) continue;
                g_context->IASetVertexBuffers(0, 1, m.vb.GetAddressOf(), &rstride, &roffset);
                g_context->IASetIndexBuffer(m.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
                g_context->DrawIndexed(m.indexCount, 0, 0);
            }
        }
        // 还原 cb.view
        {
            D3D11_MAPPED_SUBRESOURCE m4;
            if (SUCCEEDED(g_context->Map(g_cbuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m4))) {
                memcpy(m4.pData, &cb, sizeof(cb));
                g_context->Unmap(g_cbuffer.Get(), 0);
            }
        }
        g_context->RSSetState(g_rasterState.Get());
        D3D11_VIEWPORT vpm2 = {};
        vpm2.Width = (float)g_width; vpm2.Height = (float)g_height;
        vpm2.MinDepth = 0; vpm2.MaxDepth = 1;
        g_context->RSSetViewports(1, &vpm2);
    }

    UINT stride = sizeof(Vertex), offset = 0;
    g_context->IASetInputLayout(g_inputLayout.Get());
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* cbufs[] = { g_cbuffer.Get() };
    g_context->VSSetConstantBuffers(0, 1, cbufs);
    g_context->PSSetConstantBuffers(0, 1, cbufs);

    // ★ 1. 清场 + 绑离屏 RT + 清屏(必须最先做)
    {
        ID3D11ShaderResourceView* nullSRV0[1] = { nullptr };
        g_context->PSSetShaderResources(0, 1, nullSRV0);          // 清掉上一帧后处理的 g_offscreenSRV
        ID3D11RenderTargetView* offRtvs[1] = { g_offscreenRTV.Get() };
        g_context->OMSetRenderTargets(1, offRtvs, g_offscreenDSV.Get());
        g_context->ClearRenderTargetView(g_offscreenRTV.Get(), clearColor);
        g_context->ClearDepthStencilView(g_offscreenDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    }

    // ★ 2. 再绑本帧所有 SRV/采样器(顺序现在无所谓了,反正没人再动 t0)
    ID3D11ShaderResourceView* srvs[] = { g_texArraySRV.Get() };
    g_context->PSSetShaderResources(0, 1, srvs);
    ID3D11ShaderResourceView* giSrvs[] = { g_giSRV };
    g_context->PSSetShaderResources(10, 1, giSrvs);
    ID3D11ShaderResourceView* shSrvs[] = { g_shadowSRV };
    g_context->PSSetShaderResources(11, 1, shSrvs);
    ID3D11SamplerState* samps[] = { g_sampler.Get() };
    g_context->PSSetSamplers(0, 1, samps);
    g_context->PSSetSamplers(1, 1, samps);

    // ★ 阴影 pass 资源(b1 cb / t12 深度图 / s2 点采样)
    if (g_shadowCB) {
        ID3D11Buffer* sb1[] = { g_shadowCB };
        g_context->VSSetConstantBuffers(1, 1, sb1);
        g_context->PSSetConstantBuffers(1, 1, sb1);
    }
    if (g_shadowSRVMap) g_context->PSSetShaderResources(12, 1, &g_shadowSRVMap);
    if (g_shadowSamp)   g_context->PSSetSamplers(2, 1, &g_shadowSamp);
    if (g_cloudShadowSRV) g_context->PSSetShaderResources(13, 1, &g_cloudShadowSRV);
    if (g_cloudShadowSamp) g_context->PSSetSamplers(3, 1, &g_cloudShadowSamp);


    // 顺手把下面这几行残留注释删掉(它们下面的深度状态行保留)
    // g_context->OMSetDepthStencilState(g_dssOpaque.Get(), 0);   ← 这行保留


        // ---- 天空 pass ----
    if (!underwater && g_dbgSky) {
        g_context->OMSetDepthStencilState(g_skyDSS.Get(), 0);
        g_context->IASetInputLayout(nullptr);
        g_context->VSSetShader(g_fsVS.Get(), nullptr, 0);
        g_context->PSSetShader(g_skyPS.Get(), nullptr, 0);
        g_context->Draw(3, 0);
        g_context->OMSetDepthStencilState(g_dssOpaque.Get(), 0);
    }





    vector<pair<uint64_t, ActiveChunk*>> opaqueList, transList;
    {
        lock_guard<mutex> lk(g_meshMutex);
        for (auto& kv : g_activeMeshes) {
            uint64_t key = kv.first;
            ActiveChunk* ac = &kv.second;
            int cx = (int)(key >> 32);
            int cz = (int)(key & 0xFFFFFFFF);
            XMFLOAT3 center(cx * 16.0f + 8.0f, (RY0 + RY1) * 0.5f, cz * 16.0f + 8.0f);
            float halfDiagXZ = 8.0f * 1.4142135f;
            float halfH = (RY1 - RY0) * 0.5f;
            float radius = sqrtf(halfDiagXZ * halfDiagXZ + halfH * halfH);
            if (!sphereInFrustum(planes, center, radius)) continue;

            float dx = center.x - eyePos.x;
            float dy = center.y - eyePos.y;
            float dz = center.z - eyePos.z;
            float dist = sqrtf(dx*dx + dy*dy + dz*dz);
            if (dist - radius > g_renderDistance) continue;

            if (ac->opaque.valid) opaqueList.push_back({key, ac});
            if (ac->trans.valid)  transList.push_back({key, ac});
        }
    }

    // 渲染不透明方块
    g_context->VSSetShader(g_vs.Get(), nullptr, 0);
    g_context->PSSetShader(g_ps.Get(), nullptr, 0);
    g_context->OMSetDepthStencilState(g_dssOpaque.Get(), 0);
    float bf[4] = {0,0,0,0};
    g_context->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);

    for (auto& p : opaqueList) {
        ChunkMesh& m = p.second->opaque;
        g_context->IASetVertexBuffers(0, 1, m.vb.GetAddressOf(), &stride, &offset);
        g_context->IASetIndexBuffer(m.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
        g_context->DrawIndexed(m.indexCount, 0, 0);
    }

    printf("[DRAW] opaque=%zu trans=%zu skyPS=%s fsPS=%s\n",
        opaqueList.size(), transList.size(),
        g_skyPS ? "OK" : "NULL", g_fsPS ? "OK" : "NULL");


    // 排序透明方块
    sort(transList.begin(), transList.end(),
         [&eyePos](const pair<uint64_t, ActiveChunk*>& a, const pair<uint64_t, ActiveChunk*>& b) {
        int acx = (int)(a.first >> 32), acz = (int)(a.first & 0xFFFFFFFF);
        int bcx = (int)(b.first >> 32), bcz = (int)(b.first & 0xFFFFFFFF);
        float ax = acx * 16.0f + 8.0f, az = acz * 16.0f + 8.0f;
        float bx = bcx * 16.0f + 8.0f, bz = bcz * 16.0f + 8.0f;
        float da = (ax - eyePos.x) * (ax - eyePos.x) + (az - eyePos.z) * (az - eyePos.z);
        float db = (bx - eyePos.x) * (bx - eyePos.x) + (bz - eyePos.z) * (bz - eyePos.z);
        return da > db;
    });

    // ★ G3:拷贝深度并绑定 t3(水面厚度采样;此时深度里只有不透明地形)
    if (g_depthCopyTex && g_depthCopySRV && g_offscreenDepthTex) {
        g_context->CopyResource(g_depthCopyTex, g_offscreenDepthTex.Get());
        g_context->PSSetShaderResources(3, 1, &g_depthCopySRV);
    }

    // 渲染透明方块
    g_context->VSSetShader(g_vs.Get(), nullptr, 0);
    g_context->PSSetShader(g_psTrans.Get(), nullptr, 0);
    g_context->OMSetDepthStencilState(g_dssTrans.Get(), 0);
    g_context->OMSetBlendState(g_blendState.Get(), bf, 0xFFFFFFFF);

    for (auto& p : transList) {
        ChunkMesh& m = p.second->trans;
        g_context->IASetVertexBuffers(0, 1, m.vb.GetAddressOf(), &stride, &offset);
        g_context->IASetIndexBuffer(m.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
        g_context->DrawIndexed(m.indexCount, 0, 0);
    }

    // ---- 粒子 ----
    {
        D3D11_MAPPED_SUBRESOURCE mapVB;
        if (SUCCEEDED(g_context->Map(g_particleVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapVB))) {
            ParticleVertex* v = (ParticleVertex*)mapVB.pData;
            static const float corners[4][2] = {{-1,-1},{1,-1},{1,1},{-1,1}};

            int count = 0;
            for (int i = 0; i < MAX_PARTICLES && count < MAX_PARTICLES; i++) {
                Particle& p = g_particles[i];
                if (!p.active) continue;
                float lr = p.life / p.maxLife;
                float alpha = (lr > 0.5f) ? 1.0f : (lr * 2.0f);
                for (int c = 0; c < 4; c++) {
                    ParticleVertex& pv = v[count*4 + c];
                    pv.center = p.pos;
                    pv.size = p.size;
                    pv.corner = XMFLOAT2(corners[c][0], corners[c][1]);
                    pv.color = p.color;
                    pv.alpha = alpha;
                }
                count++;
            }
            g_context->Unmap(g_particleVB.Get(), 0);

            if (count > 0) {
                cb.cameraRight = camRight;
                cb.cameraUp = camUp;
                D3D11_MAPPED_SUBRESOURCE m2;
                g_context->Map(g_cbuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m2);
                memcpy(m2.pData, &cb, sizeof(cb));
                g_context->Unmap(g_cbuffer.Get(), 0);

                // 设置粒子专属的渲染状态
                g_context->RSSetState(g_rasterNoCull.Get());
                g_context->IASetInputLayout(g_particleLayout.Get());
                
                // ★ 核心修复：粒子的 stride 必须是 sizeof(ParticleVertex)！
                UINT particleStride = sizeof(ParticleVertex);
                UINT particleOffset = 0;
                g_context->IASetVertexBuffers(0, 1, g_particleVB.GetAddressOf(), &particleStride, &particleOffset);
                
                g_context->IASetIndexBuffer(g_particleIB.Get(), DXGI_FORMAT_R32_UINT, 0);
                g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                g_context->VSSetShader(g_particleVS.Get(), nullptr, 0);
                g_context->PSSetShader(g_particlePS.Get(), nullptr, 0);
                g_context->OMSetDepthStencilState(g_dssTrans.Get(), 0);
                g_context->OMSetBlendState(g_blendState.Get(), bf, 0xFFFFFFFF);
                g_context->DrawIndexed(count * 6, 0, 0);
            }
        }
    }
    // ---- BV 积云 pass(粒子之后、后处理之前)----

        if (!underwater && g_dbgCloud) {   // F1 可关(定位"糊脸"层)
            ID3D11RenderTargetView* cRtv[1] = { g_offscreenRTV.Get() };
            g_context->OMSetRenderTargets(1, cRtv, nullptr);      // 摘 DSV,深度改 SRV 读
            g_context->RSSetState(g_rasterNoCull.Get());          // 全屏三角不挑环绕
            g_context->OMSetDepthStencilState(g_skyDSS.Get(), 0); // 不写不测深度
            g_context->IASetInputLayout(nullptr);
            g_context->VSSetShader(g_fsVS.Get(), nullptr, 0);
            g_context->PSSetShader(g_cloudPS.Get(), nullptr, 0);
            g_context->PSSetShaderResources(3, 1, g_depthSRV.GetAddressOf());
            g_context->Draw(3, 0);
            // ★ 必须解绑深度 SRV: 下一帧要把同一张深度当 DSV 用,
            //   D3D11 检测到读写冲突会强制置 null 并在 debug layer 报 D3D11_MESSAGE_ID
            ID3D11ShaderResourceView* nullT3[1] = { nullptr };
            g_context->PSSetShaderResources(3, 1, nullT3);
            g_context->OMSetDepthStencilState(g_dssOpaque.Get(), 0);
        }

        // ================= 后处理阶段 =================

    // ================= Pass 9: TAA(历史帧解析) =================
    if (g_taaRTV && g_taaSRV && g_histSRV[g_histIdx] && g_taaPS) {
        ID3D11RenderTargetView* trtv[1] = { g_taaRTV };
        g_context->OMSetRenderTargets(1, trtv, nullptr);
        g_context->IASetInputLayout(nullptr);
        g_context->VSSetShader(g_fsVS.Get(), nullptr, 0);
        g_context->PSSetShader(g_taaPS, nullptr, 0);
        ID3D11ShaderResourceView* s0v[] = { g_offscreenSRV.Get() };
        g_context->PSSetShaderResources(0, 1, s0v);
        ID3D11ShaderResourceView* s1v[] = { g_histSRV[g_histIdx] };
        g_context->PSSetShaderResources(3, 1, s1v);
        g_context->Draw(3, 0);
        g_context->CopyResource(g_histTex[g_histIdx], g_taaTex);   // 本帧结果 → 历史
        ID3D11ShaderResourceView* n3[1] = { nullptr };
        g_context->PSSetShaderResources(3, 1, n3);
    }

    // ================= Pass 7/8: God rays + Bloom(天空/场景 HDR 后处理) =================
    if (!underwater && g_offscreenSRV && g_godRTV && g_bloomRTV) {
        ID3D11RenderTargetView* grtv[1] = { g_godRTV };
        g_context->OMSetRenderTargets(1, grtv, nullptr);
        g_context->IASetInputLayout(nullptr);
        g_context->VSSetShader(g_fsVS.Get(), nullptr, 0);
        if (g_godPS) {
            g_context->PSSetShader(g_godPS, nullptr, 0);
            ID3D11ShaderResourceView* s0v[] = { g_offscreenSRV.Get() };
            g_context->PSSetShaderResources(0, 1, s0v);
            if (g_shadowCB) {
                ID3D11Buffer* b1s[] = { g_shadowCB };
                g_context->PSSetConstantBuffers(1, 1, b1s);
            }
            g_context->Draw(3, 0);
        }
        ID3D11RenderTargetView* brtv[1] = { g_bloomRTV };
        g_context->OMSetRenderTargets(1, brtv, nullptr);
        if (g_bloomPS) {
            g_context->PSSetShader(g_bloomPS, nullptr, 0);
            g_context->Draw(3, 0);
        }
    }


    // 解绑所有 RTV（防止冲突）
    ID3D11RenderTargetView* nullRTV[] = { nullptr };
    g_context->OMSetRenderTargets(1, nullRTV, nullptr);

    // 绑定回主屏幕
    ID3D11RenderTargetView* mainRtvs[] = { g_rtv.Get() };
    g_context->OMSetRenderTargets(1, mainRtvs, nullptr);
    
    // 设置全屏渲染状态
    g_context->IASetInputLayout(nullptr);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_fsVS.Get(), nullptr, 0);
    g_context->PSSetShader(g_fsPS.Get(), nullptr, 0);
    
    ID3D11ShaderResourceView* sceneSrc = (TAA_ENABLE && g_taaSRV) ? g_taaSRV : g_offscreenSRV.Get();
    ID3D11ShaderResourceView* postSrvs[] = { sceneSrc, g_bloomSRV, g_godSRV };
    g_context->PSSetShaderResources(0, 3, postSrvs);
    ID3D11SamplerState* postSamps[] = { g_fsSampler.Get() };
    g_context->PSSetSamplers(0, 1, postSamps);

    // 绘制全屏三角形 (ACES色调映射)
    g_context->Draw(3, 0); 

    // 解绑，准备绘制 HUD
    ID3D11RenderTargetView* nullRTV2[] = { nullptr };
    g_context->OMSetRenderTargets(1, nullRTV2, nullptr);
    g_context->Flush();
    
    // 绘制 HUD
    drawHudAndCrosshair(fps);

    // 提交显示
    g_swapChain->Present(1, 0);
}

// ============================================================
// Section 13. 输入
// ============================================================
static void captureMouse(HWND hwnd, bool on) {
    if (on) {
        SetCapture(hwnd);
        RECT r;
        GetClientRect(hwnd, &r);
        POINT tl = { r.left, r.top };
        POINT br = { r.right, r.bottom };
        ClientToScreen(hwnd, &tl);
        ClientToScreen(hwnd, &br);
        RECT cr = { tl.x, tl.y, br.x, br.y };
        ClipCursor(&cr);
        ShowCursor(FALSE);
        G::mouseCaptured = true;

        int cx = (r.right + r.left) / 2;
        int cy = (r.bottom + r.top) / 2;
        POINT c = { cx, cy };
        ClientToScreen(hwnd, &c);
        SetCursorPos(c.x, c.y);
        G::lastMouseX = cx;
        G::lastMouseY = cy;
    } else {
        ReleaseCapture();
        ClipCursor(nullptr);
        ShowCursor(TRUE);
        G::mouseCaptured = false;
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_KEYDOWN:
        //case WM_KEYDOWN:
            if (w == VK_OEM_3 && !(l & 0x40000000)) {
                g_consoleOpen = !g_consoleOpen;
                g_cmdBuf.clear();
                if (g_consoleOpen) { g_oldImc = ImmAssociateContext(hwnd, nullptr); }
                else { ImmAssociateContext(hwnd, g_oldImc); g_oldImc = nullptr; }
                return 0;
            }



            if (g_consoleOpen) {
                if (w == VK_RETURN) { execCommand(g_cmdBuf); g_cmdBuf.clear(); return 0; }
                if (w == VK_ESCAPE) { g_consoleOpen = false; return 0; }
                if (w == VK_BACK && !g_cmdBuf.empty()) g_cmdBuf.pop_back();
                return 0;                          // 控制台开着:不进 keys[],游戏键全部失效
            }
            //if (w < 256) { /* 原有代码原封不动 */ }

            if (w < 256) {
                bool wasDown = G::keys[w];
                G::keys[w] = true;
                if (!wasDown) {
                    if (GetKeyState(VK_CONTROL) & 0x8000) {   // ★ 调试用组合键(功能键全被系统占用)
                        if (w == 'K') { g_dbgCloud = !g_dbgCloud;  printf("[DBG] cloud=%d\n",  (int)g_dbgCloud); }
                        else if (w == 'L') { g_dbgSky = !g_dbgSky;      printf("[DBG] sky=%d\n",    (int)g_dbgSky); }
                        else if (w == 'M') { g_dbgShadow = !g_dbgShadow; printf("[DBG] shadow=%d\n", (int)g_dbgShadow); }
                        else if (w == 'J') { g_dbgFog = !g_dbgFog;       printf("[DBG] fog=%d\n",    (int)g_dbgFog); }
                    } else {
                        if (w == 'M') switchMode(1);
                        else if (w == 'N') switchMode(0);
                    }
                }
            }
            if (w == VK_ESCAPE) {
                if (G::mouseCaptured) captureMouse(hwnd, false);
                else PostQuitMessage(0);
            }
            return 0;
        case WM_CHAR:
            if (g_consoleOpen) {
                if (l & 0x40000000) return 0;    // 位30=1:按键重复,丢弃
                if ((int)w >= 32 && (int)w < 127 && (int)w != '`') g_cmdBuf.push_back((char)w);
                return 0;
            }
            break;
            // 不开控制台 → 落到 DefWindowProc
        case WM_KEYUP:
            if (w < 256) G::keys[w] = false;      // ← 原有代码不动

            return 0;

        case WM_LBUTTONDOWN:
            if (g_consoleOpen) return 0;
            if (!G::mouseCaptured) captureMouse(hwnd, true);
            else G::leftMouseDown = true;
            return 0;
        case WM_RBUTTONDOWN:
            if (G::mouseCaptured) G::rightMouseDown = true;
            return 0;
        case WM_LBUTTONUP:
            G::leftMouseDown = false;
            return 0;
        case WM_RBUTTONUP:
            G::rightMouseDown = false;
            return 0;

        case WM_MOUSEWHEEL: {
            if (g_consoleOpen) return 0;
            if (!G::mouseCaptured) return 0;
            int delta = GET_WHEEL_DELTA_WPARAM(w);
            if (delta > 0) {
                G::heldBlockIndex = (G::heldBlockIndex + 1) % g_selectableCount;
            } else if (delta < 0) {
                G::heldBlockIndex = (G::heldBlockIndex - 1 + g_selectableCount) % g_selectableCount;
            }
            G::heldBlock = g_selectableBlocks[G::heldBlockIndex];
            return 0;
        }

        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(l);
            int my = GET_Y_LPARAM(l);

            if (G::mouseCaptured) {
                int dx = mx - G::lastMouseX;
                int dy = my - G::lastMouseY;

                G::yaw   += dx * 0.005f;
                G::pitch -= dy * 0.005f;
                G::pitch  = max(-1.5f, min(1.5f, G::pitch));

                RECT r;
                GetClientRect(hwnd, &r);
                int cx = (r.right + r.left) / 2;
                int cy = (r.bottom + r.top) / 2;
                POINT c = { cx, cy };
                ClientToScreen(hwnd, &c);
                SetCursorPos(c.x, c.y);

                G::lastMouseX = cx;
                G::lastMouseY = cy;
            } else {
                G::lastMouseX = mx;
                G::lastMouseY = my;
            }
            return 0;
        }

        case WM_KILLFOCUS:
            if (G::mouseCaptured) captureMouse(hwnd, false);
            for (int i = 0; i < 256; i++) G::keys[i] = false;
            G::leftMouseDown = G::rightMouseDown = false;
            return 0;

        case WM_DESTROY:
            captureMouse(hwnd, false);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, w, l);
}

// ============================================================
// Section 14. 主程序
// ============================================================
static uint64_t makeRandomSeed() {
    std::random_device rd;
    uint64_t a = ((uint64_t)rd() << 32) | (uint64_t)rd();
    uint64_t b = (uint64_t)chrono::high_resolution_clock::now()
                     .time_since_epoch().count();
    uint64_t c = (uint64_t)GetTickCount64() * 0x9E3779B97F4A7C15ULL;
    uint64_t s = a ^ b ^ c;
    if (s == 0) s = 0xFEEDFACE12345678ULL;
    return s;
}

enum class StartChoice { CreateNew, OpenExisting, Cancelled };

static StartChoice askStartChoice() {
    int r = MessageBoxW(nullptr,
        L"请选择操作：\n\n"
        L"  [是]    ——  创建新世界\n"
        L"  [否]    ——  打开已有存档 (.map)\n"
        L"  [取消]  ——  退出",
        L"Voxel World — 启动",
        MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);

    if (r == IDYES) return StartChoice::CreateNew;
    if (r == IDNO)  return StartChoice::OpenExisting;
    return StartChoice::Cancelled;
}

static wstring askNewPath() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[MAX_PATH];
    swprintf_s(buf, MAX_PATH, L"%04d%02d%02d%02d%02d.map",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Voxel Map (*.map)\0*.map\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"map";
    ofn.lpstrTitle = L"输入新世界文件名";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameW(&ofn)) return wstring(buf);
    return wstring();
}

static wstring askOpenPath() {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Voxel Map (*.map)\0*.map\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"map";
    ofn.lpstrTitle = L"选择要加载的存档";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) return wstring(buf);
    return wstring();
}

void LoadModConfig(const std::string& modPath) {
    std::ifstream f(modPath);
    if (!f.is_open()) return;
    
    try {
        json j;
        f >> j;
        
        if (j.contains("Sky") && j["Sky"].contains("ColorR")) {
            g_modSettings.skyColor[0] = j["Sky"]["ColorR"];
            g_modSettings.skyColor[1] = j["Sky"]["ColorG"];
            g_modSettings.skyColor[2] = j["Sky"]["ColorB"];
        }
        
        if (j.contains("Fog")) {
            g_modSettings.fogEnable = j["Fog"]["Enable"];
            g_modSettings.fogDensity = j["Fog"]["Density"];
            g_modSettings.fogColor[0] = j["Fog"]["ColorR"];
            g_modSettings.fogColor[1] = j["Fog"]["ColorG"];
            g_modSettings.fogColor[2] = j["Fog"]["ColorB"];
        }
        
        if (j.contains("Terrain")) {
            g_modSettings.noiseAmplitude = j["Terrain"]["NoiseAmplitude"];
            g_modSettings.flatMode = j["Terrain"]["FlatMode"];
            // ★ 新增（加上容错判断）
            if (j["Terrain"].contains("NoHeightLimit")) g_modSettings.noHeightLimit = j["Terrain"]["NoHeightLimit"];
            if (j["Terrain"].contains("GridMode")) g_modSettings.gridMode = j["Terrain"]["GridMode"];
        }
        
        g_modSettings.loaded = true;
        printf("[MOD] 成功加载 Mod 配置: %s\n", modPath.c_str());
    } catch (std::exception& e) {
        printf("[MOD] 加载 Mod 失败: %s\n", e.what());
    }
}

// 弹出文件选择框来加载 Mod
void PromptAndLoadMod() {
    OPENFILENAMEW ofn = {};
    wchar_t szFile[260] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = L"Mod Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        char path[260];
        WideCharToMultiByte(CP_UTF8, 0, ofn.lpstrFile, -1, path, 260, NULL, NULL);
        LoadModConfig(path); // 调用你之前写好的加载函数
    }
}




int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetProcessDPIAware();
    auto tStart = chrono::steady_clock::now();

    initBlockInfo();

    // ===== 替换为文件选择框 =====
    PromptAndLoadMod(); 
    // ============================

    StartChoice startChoice = askStartChoice();
    // ... 下面的代码保持不变 ...
    // ... 下面的代码保持不变 ...
    if (startChoice == StartChoice::Cancelled) return 0;

    wstring savePath;
    bool isNew = false;

    if (startChoice == StartChoice::CreateNew) {
        savePath = askNewPath();
        if (savePath.empty()) return 0;
        isNew = true;
    } else {
        savePath = askOpenPath();
        if (savePath.empty()) return 0;
        isNew = false;
    }

    printf("[SAVE] Path: %ls  (isNew=%d)\n", savePath.c_str(), (int)isNew);

    if (isNew) {
        G::seed = makeRandomSeed();
        //G::seed = 0x1234567812345678ULL;  // 固定种子，便于复现
        reseedAllNoise(G::seed);
        rollTuning(G::seed);
        //generateCanyons(G::seed);
        generateTubes(G::seed);
        G::camPos = XMFLOAT3(0.5f, 200.0f, 0.5f);

        FILE* f = _wfopen(savePath.c_str(), L"w+b");
        if (!f) {
            MessageBoxW(nullptr, L"无法创建文件。", L"错误", MB_OK | MB_ICONERROR);
            return 1;
        }
        Save::FileHeader h = {};
        memcpy(h.magic, Save::MAGIC, 8);
        h.version = Save::VERSION;
        h.seed = G::seed;
        h.tuning = g_tuning;
        h.spawnX = 0;
        h.spawnY = 200;
        h.spawnZ = 0;
        fwrite(&h, sizeof(h), 1, f);
        fclose(f);

        if (!Save::g_store.open(savePath, false)) return 1;
        printf("[SAVE] Created new world seed=%llX\n", (unsigned long long)G::seed);
    } else {
        if (!Save::g_store.open(savePath, false)) {
            MessageBoxW(nullptr, L"无法加载存档。", L"错误", MB_OK | MB_ICONERROR);
            return 1;
        }
        printf("[SAVE] Loaded world seed=%llX\n", (unsigned long long)G::seed);
    }

    auto tGen = chrono::steady_clock::now();
    printf("[TIME] Init: %.2f s\n",
           chrono::duration<double>(tGen - tStart).count());

    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"Dx11VoxelStreaming";
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(0, L"Dx11VoxelStreaming",
                                L"Voxel World — Infinite Streaming",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                g_width, g_height,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    if (!initD3D(hwnd)) return 1;
    giInit(g_width, g_height);

    // 矩阵管线自检:世界原点上方 1 格的点,应投到屏幕中心偏下一点
    {
        XMMATRIX v = XMMatrixLookToLH(XMVectorSet(0, 1, 0, 0), XMVectorSet(0, 0, 1, 0), XMVectorSet(0, 1, 0, 0));
        XMMATRIX p = XMMatrixPerspectiveFovLH(XMConvertToRadians(75.0f), 1280.0f / 720.0f, 0.1f, 2000.0f);
        XMVECTOR clip = XMVector4Transform(XMVectorSet(0, 1, 5, 1), v * p);
        XMFLOAT4 c; XMStoreFloat4(&c, clip);
        printf("[SELFTEST] ndc=(%.3f, %.3f, %.3f)  预期 y≈0.30 z≈1.0\n",
            c.x / c.w, c.y / c.w, c.z / c.w);   // y 错 → 矩阵约定翻车,开机即报
    }



    auto tD3D = chrono::steady_clock::now();
    printf("[TIME] D3D init: %.2f s\n",
           chrono::duration<double>(tD3D - tGen).count());

    printf("\n=== Controls ===\n");
    printf("  LMB      : Capture mouse / Place block\n");
    printf("  RMB      : Break block\n");
    printf("  WASD     : Move\n");
    printf("  SHIFT    : Sprint (7 blocks/sec)\n");
    printf("  SPACE    : Jump / Swim up / Fly up\n");
    printf("  M        : Survival mode (gravity + collision)\n");
    printf("  N        : Spectator mode (fly)\n");
    printf("  Q/E      : Down/Up (fly mode)\n");
    printf("  `        : Console  (/time set 0-24, /tp, /fill, /del, /room set)\n");
    printf("  ESC      : Release mouse or Quit\n\n");

    MSG msg = {};
    auto lastTime = chrono::steady_clock::now();
    int frames = 0;
    double fpsTimer = 0;
    float fps = 0.0f;
    auto lastSaveTime = chrono::steady_clock::now();
    McNoiseInit((uint64_t)time(nullptr));
    initTerrainSplines();

    startWorkers();

    while (msg.message != WM_QUIT) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (msg.message == WM_QUIT) break;

        auto now = chrono::steady_clock::now();
        float dt = chrono::duration<float>(now - lastTime).count();
        if (dt > 0.1f) dt = 0.1f;
        lastTime = now;

        // ---- 昼夜循环:时间推进(一天 24 分钟,0=午夜 12=正午) ----
        g_timeOfDay += dt / DAY_LENGTH_SEC * 24.0f;
        if (g_timeOfDay >= 24.0f) g_timeOfDay -= 24.0f;
        fpsTimer += dt;
        frames++;
        if (fpsTimer >= 1.0) {
            fps = (float)(frames / fpsTimer);
            size_t nChunks;
            {
                lock_guard<mutex> lk(G::chunksMutex);
                nChunks = G::chunks.size();
            }
            printf("[FPS] %.1f  Chunks: %zu  Mode: %s\n",
                   fps, nChunks, (G::playerMode == 1) ? "Survival" : "Spectator");
            frames = 0; fpsTimer = 0;
        }

        {
            auto since = chrono::duration<double>(now - lastSaveTime).count();
            if (since >= 2.0) {
                flushAllDirty();
                lastSaveTime = now;
            }
        }

        updateCamera(dt);
        updateParticles(dt);
        streamingUpdate();

        if (G::leftMouseDown || G::rightMouseDown) {
            XMFLOAT3 rayOrigin = G::camPos;
            if (G::playerMode == 1) rayOrigin.y += PLAYER_EYE_HEIGHT;

            XMFLOAT3 dir(sinf(G::yaw)*cosf(G::pitch), sinf(G::pitch),
                         cosf(G::yaw)*cosf(G::pitch));
            VoxelHit hit = raycastVoxel(rayOrigin, dir, 8.0f);

            if (hit.hit) {
                if (G::leftMouseDown) {
                    if (!blockIntersectsPlayer((float)hit.px, (float)hit.py, (float)hit.pz)) {
                        setBlock(hit.px, hit.py, hit.pz, G::heldBlock);
                        markChunksDirtyAround(hit.px, hit.py, hit.pz);
                    }
                    G::leftMouseDown = false;
                } else {
                    Block broken = getBlock(hit.x, hit.y, hit.z);
                    setBlock(hit.x, hit.y, hit.z, Block::AIR);
                    markChunksDirtyAround(hit.x, hit.y, hit.z);
                    spawnBlockBreakParticles(hit.x, hit.y, hit.z, broken);
                    G::rightMouseDown = false;
                }
                flushAllDirty();
            } else {
                G::leftMouseDown = false;
                G::rightMouseDown = false;
            }
        }

        renderFrame(fps);
    }

    printf("[SAVE] Flushing all dirty chunks on exit...\n");
    flushAllDirty();
    drainSaves();
    stopWorkers();
    Save::g_store.flush();
    Save::g_store.close();

    printf("[SAVE] Done.\n");

    return 0;
}