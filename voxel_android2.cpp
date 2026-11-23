// ============================================================
// voxel_android2.cpp — voxel-j.cpp 的 Vulkan Android 版
// 游戏逻辑来自 voxel-j.cpp（MC 1.21 风格世界生成）；
// 平台层来自 voxel_Vulkan_android.cpp（ANativeActivity + Vulkan），
// 并修复了窗口重建(TERM/INIT_WINDOW)导致的 VMA 内存池断言闪退。
// 包名: com.voxel.android2.0  存档目录: /Android/data/com.voxel.android2.0/
// ============================================================

// stb 头文件（放在本文件所在目录）
#define STB_IMAGE_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>


#include <android/native_window.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "spv_voxel2_vert.h"
#include "spv_voxel2_frag.h"
#include "spv_particle_vert.h"
#include "spv_particle_frag.h"
#include "spv_hud_vert.h"
#include "spv_hud_frag.h"

#include "stb_image.h"
#include "stb_truetype.h"

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
#include <cstdarg>
#include <cwchar>
#include <cstdlib>
#include <climits>
#include <queue>
#include <condition_variable>
#include <deque>
#include <sys/stat.h>
#include <dirent.h>

#include "json.hpp"   // Mod 配置(json)

using namespace std;
using json = nlohmann::json;

// ANativeActivity / app 胶水全局指针
static struct android_app* g_app = nullptr;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "Voxel2", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "Voxel2", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Voxel2", __VA_ARGS__)

// Android 上无 windows.h，VK_* 键值需自行定义
#ifndef VK_SPACE
#define VK_SPACE 32
#endif
#ifndef VK_SHIFT
#define VK_SHIFT 16
#endif

// ============================================================
// Section 0. 最小数学库（替代 DirectXMath，列主序，兼容 Vulkan）
// ============================================================
struct XMFLOAT2 {
    float x, y;
    XMFLOAT2() : x(0), y(0) {}
    XMFLOAT2(float a, float b) : x(a), y(b) {}
};
struct XMFLOAT3 {
    float x, y, z;
    XMFLOAT3() : x(0), y(0), z(0) {}
    XMFLOAT3(float a, float b, float c) : x(a), y(b), z(c) {}
};
struct XMFLOAT4 {
    float x, y, z, w;
    XMFLOAT4() : x(0), y(0), z(0), w(0) {}
    XMFLOAT4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
};
// 行主序 4x4（供 extractFrustumPlanes 使用，与 DirectXMath 同名）
struct XMFLOAT4X4 {
    float _11, _12, _13, _14;
    float _21, _22, _23, _24;
    float _31, _32, _33, _34;
    float _41, _42, _43, _44;
};
struct XMMATRIX {
    float m[16]; // 列主序 m[col*4+row]
};

inline XMMATRIX XMMatrixIdentity() {
    XMMATRIX r = {};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

inline XMFLOAT3 operator+(const XMFLOAT3& a, const XMFLOAT3& b) {
    return XMFLOAT3(a.x + b.x, a.y + b.y, a.z + b.z);
}
inline XMFLOAT3 operator-(const XMFLOAT3& a, const XMFLOAT3& b) {
    return XMFLOAT3(a.x - b.x, a.y - b.y, a.z - b.z);
}
inline XMFLOAT3 operator*(const XMFLOAT3& a, float s) {
    return XMFLOAT3(a.x * s, a.y * s, a.z * s);
}

inline XMFLOAT3 cross3(const XMFLOAT3& a, const XMFLOAT3& b) {
    return XMFLOAT3(a.y * b.z - a.z * b.y,
                    a.z * b.x - a.x * b.z,
                    a.x * b.y - a.y * b.x);
}
inline float dot3(const XMFLOAT3& a, const XMFLOAT3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline float length3(const XMFLOAT3& a) {
    return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
}
inline XMFLOAT3 normalize3(const XMFLOAT3& a) {
    float l = length3(a);
    if (l < 1e-8f) return XMFLOAT3(0, 0, 0);
    return XMFLOAT3(a.x / l, a.y / l, a.z / l);
}

// 左手系 LookToLH：z=dir，x=cross(up,z)，y=cross(z,x)
inline XMMATRIX XMMatrixLookToLH(const XMFLOAT3& eye, const XMFLOAT3& dir, const XMFLOAT3& up) {
    XMFLOAT3 z = normalize3(dir);
    XMFLOAT3 x = normalize3(cross3(up, z));
    XMFLOAT3 y = cross3(z, x);
    XMMATRIX r;
    r.m[0] = x.x; r.m[1] = y.x; r.m[2] = z.x; r.m[3] = 0;
    r.m[4] = x.y; r.m[5] = y.y; r.m[6] = z.y; r.m[7] = 0;
    r.m[8] = x.z; r.m[9] = y.z; r.m[10] = z.z; r.m[11] = 0;
    r.m[12] = -dot3(x, eye);
    r.m[13] = -dot3(y, eye);
    r.m[14] = -dot3(z, eye);
    r.m[15] = 1.0f;
    return r;
}

// 左手系透视（Vulkan 深度范围 z∈[0,1]），fovY 弧度
inline XMMATRIX XMMatrixPerspectiveFovLH(float fovY, float aspect, float zn, float zf) {
    float h = 1.0f / tanf(fovY * 0.5f);
    float w = h / aspect;
    XMMATRIX r = {};
    r.m[0] = w;
    r.m[5] = h;
    r.m[10] = zf / (zf - zn);
    r.m[11] = 1.0f;
    r.m[14] = -zn * zf / (zf - zn);
    return r;
}

inline XMMATRIX operator*(const XMMATRIX& a, const XMMATRIX& b) {
    XMMATRIX r = {};
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            float v = 0;
            for (int k = 0; k < 4; k++)
                v += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = v;
        }
    }
    return r;
}

inline float XMConvertToRadians(float deg) { return deg * 3.14159265358979323846f / 180.0f; }

// 列主序 XMMATRIX → 行主序 XMFLOAT4X4（与 DirectXMath XMStoreFloat4x4 一致）
inline void XMStoreFloat4x4(XMFLOAT4X4* o, const XMMATRIX& m) {
    o->_11 = m.m[0];  o->_12 = m.m[4];  o->_13 = m.m[8];  o->_14 = m.m[12];
    o->_21 = m.m[1];  o->_22 = m.m[5];  o->_23 = m.m[9];  o->_24 = m.m[13];
    o->_31 = m.m[2];  o->_32 = m.m[6];  o->_33 = m.m[10]; o->_34 = m.m[14];
    o->_41 = m.m[3];  o->_42 = m.m[7];  o->_43 = m.m[11]; o->_44 = m.m[15];
}


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

static const int LOAD_RADIUS   = 6;
static const int UNLOAD_RADIUS = 9;
static const int LOAD_BUDGET_PER_FRAME = 2;
static const int STARTUP_FAST_LOAD_UNTIL = 60;
static const int LOAD_BUDGET_FAST = 4;

static const int MAX_MESH_REBUILD_PER_FRAME = 8;

static const float RENDER_DISTANCE = 320.0f;

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

// Android 版方块名（ASCII，UTF-8；fonts 无 CJK 字形时用英文）
static const char* blockNameCN(Block b) {
    switch (b) {
        case Block::STONE:           return "Stone";
        case Block::DEEPSLATE:       return "Deepslate";
        case Block::TUFF:            return "Tuff";
        case Block::CALCITE:         return "Calcite";
        case Block::ANDESITE:        return "Andesite";
        case Block::DIORITE:         return "Diorite";
        case Block::GRANITE:         return "Granite";
        case Block::DIRT:            return "Dirt";
        case Block::GRASS:           return "Grass";
        case Block::PODZOL:          return "Podzol";
        case Block::MYCELIUM:        return "Mycelium";
        case Block::MUD:             return "Mud";
        case Block::SAND:            return "Sand";
        case Block::RED_SAND:        return "RedSand";
        case Block::GRAVEL:          return "Gravel";
        case Block::CLAY:            return "Clay";
        case Block::SANDSTONE:       return "Sandstone";
        case Block::RED_SANDSTONE:   return "RedSandstone";
        case Block::TERRACOTTA:      return "Terracotta";
        case Block::SNOW_BLOCK:      return "Snow";
        case Block::ICE:             return "Ice";
        case Block::PACKED_ICE:      return "PackedIce";
        case Block::BLUE_ICE:        return "BlueIce";
        case Block::WATER:           return "Water";
        case Block::LAVA:            return "Lava";
        case Block::COAL_ORE:        return "CoalOre";
        case Block::IRON_ORE:        return "IronOre";
        case Block::COPPER_ORE:      return "CopperOre";
        case Block::GOLD_ORE:        return "GoldOre";
        case Block::REDSTONE_ORE:    return "RedstoneOre";
        case Block::LAPIS_ORE:       return "LapisOre";
        case Block::DIAMOND_ORE:     return "DiamondOre";
        case Block::EMERALD_ORE:     return "EmeraldOre";
        case Block::OAK_LOG:         return "OakLog";
        case Block::OAK_LEAVES:      return "OakLeaves";
        case Block::SPRUCE_LOG:      return "SpruceLog";
        case Block::SPRUCE_LEAVES:   return "SpruceLeaves";
        case Block::BIRCH_LOG:       return "BirchLog";
        case Block::BIRCH_LEAVES:    return "BirchLeaves";
        case Block::JUNGLE_LOG:      return "JungleLog";
        case Block::JUNGLE_LEAVES:   return "JungleLeaves";
        case Block::ACACIA_LOG:      return "AcaciaLog";
        case Block::ACACIA_LEAVES:   return "AcaciaLeaves";
        case Block::DARK_OAK_LOG:    return "DarkOakLog";
        case Block::DARK_OAK_LEAVES: return "DarkOakLeaves";
        case Block::CACTUS:          return "Cactus";
        case Block::DEAD_BUSH:       return "DeadBush";
        case Block::SHORT_GRASS:     return "Grass";
        case Block::TALL_GRASS:      return "TallGrass";
        case Block::FERN:            return "Fern";
        case Block::MOSS_CARPET:     return "MossCarpet";
        case Block::MUSHROOM_RED:    return "Mushroom";
        case Block::MUSHROOM_BROWN:  return "MushroomB";
        case Block::SUGAR_CANE:      return "SugarCane";
        case Block::BAMBOO:          return "Bamboo";
        case Block::VINE:            return "Vine";
        case Block::GLASS:           return "Glass";
        case Block::BRICK:           return "Brick";
        case Block::PLANKS:          return "Planks";
        case Block::MISSING:         return "Missing";
    }
    return "?";
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
    atomic<bool> lightDirty{ false };       // 光照需要重算

    atomic<bool>    dirty{false};

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
Noise2D nTemp, nHum, nCont, nEro, nWeird, nDetail, nSpike, nEntry;
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

bool placeDown = false;
bool breakDown = false;

// 改前:G 里没有
// 改后(加在 G::heldBlockIndex 声明附近):
bool jumpHeld = false;        // 触摸跳跃按钮按住状态
bool crouchHeld = false;      // ★ 新增:触摸下蹲按钮按住状态
bool waterJumpLatch = false;  // 水面跳出防连发


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
    if (b != Block::AIR) {   // 放方块可能抬高列顶
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
// ============================================================
// Section 6. 世界生成
// ============================================================
struct Tuning {
    int   canyonCount;
    int   canyonSegMin, canyonSegMax;
    float canyonSegLenMin, canyonSegLenMax, canyonTurnAmp;
    float canyonWidthMin, canyonWidthMax;
    int   canyonYTopMin, canyonYTopMax, canyonYBotMin, canyonYBotMax;

    int   tubeCount, tubeLenMin, tubeLenMax;
    float tubeTurnYaw, tubeTurnPitch;
    int   tubeRMin, tubeRMax;
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



// ★ 地表高度缓存:同一列全图只算一次(Aquifer 13邻扫列/平滑重叠区全部命中)
static mutex g_surfCacheMutex;
static unordered_map<uint64_t, float> g_surfCache;
static float mcSurfaceHeightCached(float x, float z) {
    uint64_t k = ((uint64_t)(uint32_t)(int)x << 32) | (uint32_t)(int)z;
    {
        lock_guard<mutex> lk(g_surfCacheMutex);
        auto it = g_surfCache.find(k);
        if (it != g_surfCache.end()) return it->second;
    }
    float v = mcSurfaceHeight(x, z);
    lock_guard<mutex> lk(g_surfCacheMutex);
    if (g_surfCache.size() > 300000) g_surfCache.clear();
    g_surfCache[k] = v;
    return v;
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

    static int surfaceEstimate(int x, int z) { return mcSurfaceHeightCached((float)x, (float)z); }	// ★ 接你的地表估计


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
        out[ez * W + ex] = mcSurfaceHeightCached((float)wx, (float)wz);


    
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


static void generateChunkData(G::ChunkData& cd, int cx, int cz)
{
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
    for (int lz = 0; lz < CHUNK_SIZE; lz++) for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        int wx = cx * CHUNK_SIZE + lx;
        int wz = cz * CHUNK_SIZE + lz;
        //float wxF = (float)wx, wzF = (float)wz;
        cd.biomes[chunkBiomeIndex(lx, lz)] = (uint8_t)terrainModel((float)wx, (float)wz).b;

    }

    // ============================================================
    // 按列填充（原版）：实体 + 涂装 + 填水 + 冰 + 水下顶面
    // ============================================================
    for (int lz = 0; lz < CHUNK_SIZE; lz++) for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        int wx = cx * CHUNK_SIZE + lx;
        int wz = cz * CHUNK_SIZE + lz;
        Biome b = (Biome)cd.biomes[chunkBiomeIndex(lx, lz)];
        BiomeProfile p = biomeProfile(b);

        int surface = (int)centerH[lz][lx];
        //if (b == Biome::RIVER || b == Biome::FROZEN_RIVER) surface -= 4;
        if (g_modSettings.loaded && g_modSettings.noHeightLimit) {
            surface = max(RY0 + 5, surface);
            if (surface >= RY1 - 1) surface = RY1 - 2;
        } else {
            surface = max(RY0 + 5, min(RY1 - 8, surface));
        }

        for (int y = RY0; y <= surface; y++) {
            Block blk;
            if (y < -56) blk = Block::DEEPSLATE;
            else if (y < 8) blk = (rand01_3(wx,y,wz, seed32(0xAB)) < 0.15f) ? Block::TUFF : Block::DEEPSLATE;
            else blk = Block::STONE;
            if (y == surface) {
                blk = p.surface;
                if (p.snowy && y >= 68) blk = Block::SNOW_BLOCK;
            } else if (y > surface - 4) blk = p.subsurface;
            if (b == Biome::DESERT && y > surface - 6 && y < surface) blk = Block::SANDSTONE;
            if ((b == Biome::BADLANDS || b == Biome::ERODED_BADLANDS || b == Biome::WOODED_BADLANDS)
                && y > surface - 8 && y < surface)
                blk = (rand01_3(wx,y,wz, seed32(0xCD)) < 0.5f) ? Block::RED_SANDSTONE : Block::TERRACOTTA;
            cd.blocks[chunkBlockIndex(lx, y, lz)] = (uint8_t)blk;
        }
        for (int y = surface + 1; y <= SEA_LEVEL; y++)
            if (cd.blocks[chunkBlockIndex(lx, y, lz)] == (uint8_t)Block::AIR)
                cd.blocks[chunkBlockIndex(lx, y, lz)] = (uint8_t)Block::WATER;
        if (p.snowy && cd.blocks[chunkBlockIndex(lx, SEA_LEVEL, lz)] == (uint8_t)Block::WATER)
            cd.blocks[chunkBlockIndex(lx, SEA_LEVEL, lz)] = (uint8_t)Block::ICE;
        if (p.ocean && surface < SEA_LEVEL)
            cd.blocks[chunkBlockIndex(lx, surface, lz)] = (uint8_t)p.underwater;
    }

    // ============================================================
    // 峡谷后挖（原版行为：地表裂缝）
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
    string  path;
    unordered_map<uint64_t, uint64_t> chunkOffsets;
    mutex    ioMutex;

    bool open(const string& p, bool createIfMissing) {
        path = p;
        fp = fopen(p.c_str(), "r+b");
        if (!fp && createIfMissing) {
            fp = fopen(p.c_str(), "w+b");
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
        fseeko(fp, 0, SEEK_END);
        uint64_t off = (uint64_t)ftello(fp);
        ChunkRecord rec = { RECORD_TAG, cx, cz, (uint32_t)(8 + nb + ni) };
        fwrite(&rec, sizeof(rec), 1, fp);
        fwrite(&nb, 4, 1, fp);
        fwrite(&ni, 4, 1, fp);
        fwrite(encB.data(), 1, nb, fp);
        fwrite(encBi.data(), 1, ni, fp);
        chunkOffsets[chunkKey(cx, cz)] = off + sizeof(rec);
        return true;
    }
    bool readChunk(int cx, int cz, G::ChunkData& cd) {
        if (!fp) return false;
        lock_guard<mutex> lk(ioMutex);
        auto it = chunkOffsets.find(chunkKey(cx, cz));
        if (it == chunkOffsets.end()) return false;
        fseeko(fp, (long long)it->second, SEEK_SET);
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
        fseeko(fp, 0, SEEK_SET);
        fwrite(&h, sizeof(h), 1, fp);
        return true;
    }

    bool readHeader() {
        if (!fp) return false;
        fseeko(fp, 0, SEEK_SET);
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
        fseeko(fp, (long long)sizeof(FileHeader), SEEK_SET);
        ChunkRecord rec;
        while (fread(&rec, sizeof(rec), 1, fp) == 1) {
            if (rec.tag != RECORD_TAG) break;
            int64_t dataPos = ftello(fp);
            chunkOffsets[chunkKey(rec.cx, rec.cz)] = (uint64_t)dataPos;
            fseeko(fp, (long long)rec.byteCount, SEEK_CUR);
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
// Section 8. Vulkan 全局状态
// ============================================================
static const int MAX_FRAMES_IN_FLIGHT = 2;

struct ParticleVertex {
    XMFLOAT3 center;
    float    size;
    XMFLOAT2 corner;
    XMFLOAT3 color;
    float    alpha;
};

struct VkMesh {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkDeviceSize indexOffset = 0;
    uint32_t indexCount = 0;
    bool valid = false;
};
struct VkActiveChunk {
    VkMesh opaque;
    VkMesh trans;
};

struct VkState {
    bool framebufferResized = false;
    VkInstance       instance = VK_NULL_HANDLE;
    VkSurfaceKHR     surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice         device = VK_NULL_HANDLE;
    VkQueue          gfxQueue = VK_NULL_HANDLE;
    VkQueue          presentQueue = VK_NULL_HANDLE;
    uint32_t         gfxFamily = 0;
    uint32_t         presentFamily = 0;
    VmaAllocator     vma = VK_NULL_HANDLE;

    VkSwapchainKHR   swapchain = VK_NULL_HANDLE;
    VkFormat         swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D       swapExtent = {};
    vector<VkImage>        swapImages;
    vector<VkImageView>    swapViews;
    vector<VkFramebuffer>  swapFramebuffers;

    VkRenderPass     renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       voxelPipeline = VK_NULL_HANDLE;
    VkPipeline       voxelPipelineTrans = VK_NULL_HANDLE;
    VkPipeline       particlePipeline = VK_NULL_HANDLE;

    VkDescriptorSetLayout descSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkDescriptorSet  descSet = VK_NULL_HANDLE;

    VkImage          depthImage = VK_NULL_HANDLE;
    VmaAllocation    depthAlloc = VK_NULL_HANDLE;
    VkImageView      depthView = VK_NULL_HANDLE;

    VkBuffer         camUBO = VK_NULL_HANDLE;
    VmaAllocation    camUBOAlloc = VK_NULL_HANDLE;
    void*            camUBOMapped = nullptr;

    VkCommandPool    cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer  cmdBuf[MAX_FRAMES_IN_FLIGHT] = {};
    VkSemaphore      imageAvail[MAX_FRAMES_IN_FLIGHT] = {};
    vector<VkSemaphore> renderDone;
    VkFence          inFlight[MAX_FRAMES_IN_FLIGHT] = {};
    uint32_t         frameIdx = 0;

    VkImage          texArrayImage = VK_NULL_HANDLE;
    VmaAllocation    texArrayAlloc = VK_NULL_HANDLE;
    VkImageView      texArrayView = VK_NULL_HANDLE;
    VkSampler        texSampler = VK_NULL_HANDLE;
    uint32_t         texLayerCount = 0;

    VkBuffer         particleVB = VK_NULL_HANDLE;
    VmaAllocation    particleVBAlloc = VK_NULL_HANDLE;
    void*            particleVBMapped = nullptr;

    struct PendingDelete {
        VkBuffer    buffer;
        VmaAllocation alloc;
        uint64_t    frameCount;
    };
    vector<PendingDelete> pendingDeletes;
    mutex deferMutex;
    uint64_t frameCounter = 0;
};
static VkState g_vk;

static unordered_map<uint64_t, VkActiveChunk> g_activeMeshes;
static mutex g_meshMutex;

struct CameraUBO {
    XMMATRIX world;
    XMMATRIX view;
    XMMATRIX proj;
    XMFLOAT3 lightDir;   float pad1;
    XMFLOAT3 lightColor; float pad2;
    XMFLOAT3 ambientColor; float pad3;
    XMFLOAT3 cameraPos;  float pad4;
    float    depthFadeStart;  float pad5;
    float    depthFadeEnd;    float pad6;
    XMFLOAT3 cameraRight; float pad7;
    XMFLOAT3 cameraUp;    float pad8;
    XMFLOAT3 skyColor;    float fogDensity;
    XMFLOAT3 fogColor;    float underwater;
};

// ============================================================
// HUD 状态：用 stb_truetype 画文字到一个 CPU 位图，再上传到 Vulkan
// ============================================================
struct HudState {
    // CPU 位图（BGRA，与 Vulkan 上传格式一致）
    vector<uint32_t> pixels;
    int width = 0;
    int height = 0;

    // 字体
    vector<uint8_t> fontData;
    stbtt_fontinfo  fontInfo;
    float           fontSize = 24.0f;
    float           fontScale = 1.0f;
    bool            fontLoaded = false;

    // Vulkan 资源
    VkImage       image      = VK_NULL_HANDLE;
    VmaAllocation imageAlloc = VK_NULL_HANDLE;
    VkImageView   imageView  = VK_NULL_HANDLE;
    VkSampler     sampler    = VK_NULL_HANDLE;

    VkDescriptorSetLayout descSetLayout  = VK_NULL_HANDLE;
    VkDescriptorPool      descPool       = VK_NULL_HANDLE;
    VkDescriptorSet       descSet        = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            pipeline       = VK_NULL_HANDLE;

    VkBuffer      stagingBuffer  = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc   = VK_NULL_HANDLE;
    void*         stagingMapped  = nullptr;
    VkDeviceSize  stagingSize    = 0;
};
static HudState g_hud;

// Section 9. SPIR-V 加载
// ============================================================
static VkShaderModule createShaderModuleFromSPV(const uint32_t* spv, size_t words) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = words * sizeof(uint32_t);
    ci.pCode = spv;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(g_vk.device, &ci, nullptr, &m);
    return m;
}

// ============================================================
// Section 10. Vulkan 初始化
// ============================================================
static bool createSwapchain();
static bool createRenderPass();
static bool createDescriptorSetLayout();
static bool createGraphicsPipeline();
static bool createFramebuffers();
static bool createCommandPool();
static bool createSyncObjects();
static bool createDepthResources();
static bool createCameraUBO();
static bool createDescriptorPoolAndSet();
static bool createParticleVB();
static bool createTextureArray();

static bool createHudPipeline();
static bool createHudTexture(int w, int h);
static void drawHudText(float fps);
static void uploadHudTexture(VkCommandBuffer cb);
static void recreateHud(int w, int h);
static bool loadFontFromAssets();

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



// ★ double 判定 + 24 格保守余量:大坐标下 float 平面误差绝不误剔
static bool sphereInFrustumD(const FrustumPlane planes[6], const XMFLOAT3& c, float r) {
    for (int i = 0; i < 6; i++) {
        double d = (double)planes[i].n.x * c.x
            + (double)planes[i].n.y * c.y
            + (double)planes[i].n.z * c.z
            + (double)planes[i].d;
        if (d < -((double)r + 24.0)) return false;
    }
    return true;
}



// ============ 后台线程：生成+建网格在后台跑，主线程只做 GPU 上传 ============
struct UploadJob { int cx, cz; MeshData om, tm; };

static mutex g_jobMutex;
static condition_variable g_jobCV;
static deque<pair<int, int>> g_genJobs;
static deque<pair<int, int>> g_meshJobs;
static unordered_set<uint64_t> g_genQueued;
static unordered_set<uint64_t> g_meshQueued;
static mutex g_uploadMutex;
static vector<UploadJob> g_uploadJobs;
static vector<thread> g_workers;
static atomic<bool> g_workersRun{ false };

static bool loadOrGenerateChunk(int cx, int cz);   // 前向声明：实现在 Section 11

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
    {
        lock_guard<mutex> lk(G::chunksMutex);
        if (!G::chunks.count(chunkKey(cx, cz))) return;
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
        if (chunkKey(it->first, it->second) == k) it = g_genJobs.erase(it); else ++it;
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
    static int s_meshJobs = 0;
    if (s_meshJobs < 5) { LOGI("[MESH] built chunk(%d,%d) opaque=%zu trans=%zu", cx, cz, om.vertices.size(), tm.vertices.size()); s_meshJobs++; }
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
                auto f = g_genJobs.front(); g_genJobs.pop_front();
                gcx = f.first; gcz = f.second; haveGen = true;
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
            loadOrGenerateChunk(gcx, gcz);
            {
                lock_guard<mutex> lk(g_jobMutex);
                g_genQueued.erase(chunkKey(gcx, gcz));
            }
            enqueueMeshJob(gcx, gcz, false);
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
    n = (n > 4) ? 4 : (n > 1 ? n - 1 : 1);
    for (unsigned i = 0; i < n; i++) g_workers.emplace_back(workerLoop);
    printf("[THREAD] %u workers started\n", n);
}

static void stopWorkers() {
    g_workersRun = false;
    g_jobCV.notify_all();
    for (auto& t : g_workers) if (t.joinable()) t.join();
    g_workers.clear();
}

// ============================================================
// Section 11. 网格上传与释放（Vulkan/VMA，延迟释放避免等待）
// ============================================================
static unordered_set<uint64_t> g_meshesToRebuild;
static mutex g_rebuildMutex;

static bool uploadMeshToGPU_VK(const MeshData& m, VkMesh& out) {
    if (m.vertices.empty() || m.indices.empty()) { out.valid = false; return false; }
    VkDeviceSize vsz = m.vertices.size() * sizeof(Vertex);
    VkDeviceSize isz = m.indices.size() * sizeof(uint32_t);

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = vsz + isz;
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo info;
    VkBuffer buf;
    VmaAllocation alloc;
    if (vmaCreateBuffer(g_vk.vma, &bci, &ai, &buf, &alloc, &info) != VK_SUCCESS) {
        out.valid = false; return false;
    }
    memcpy(info.pMappedData, m.vertices.data(), vsz);
    memcpy((uint8_t*)info.pMappedData + vsz, m.indices.data(), isz);
    vmaFlushAllocation(g_vk.vma, alloc, 0, vsz + isz);

    out.buffer = buf;
    out.alloc = alloc;
    out.indexOffset = vsz;
    out.indexCount = (uint32_t)m.indices.size();
    out.valid = true;
    return true;
}

// ★ 延迟释放：不 vkDeviceWaitIdle，等 GPU 用完(2帧)后再销毁，避免卡顿与二次释放
static void releaseMeshVK(VkMesh& m) {
    if (m.buffer) {
        lock_guard<mutex> lk(g_vk.deferMutex);
        g_vk.pendingDeletes.push_back({ m.buffer, m.alloc,
                                        g_vk.frameCounter + MAX_FRAMES_IN_FLIGHT + 1 });
        m.buffer = VK_NULL_HANDLE;
        m.alloc = VK_NULL_HANDLE;
        m.valid = false;
    }
}

static void processPendingDeletes() {
    vector<VkState::PendingDelete> todo;
    {
        lock_guard<mutex> lk(g_vk.deferMutex);
        auto it = g_vk.pendingDeletes.begin();
        while (it != g_vk.pendingDeletes.end()) {
            if (it->frameCount <= g_vk.frameCounter) {
                todo.push_back(*it);
                it = g_vk.pendingDeletes.erase(it);
            } else ++it;
        }
    }
    for (auto& p : todo)
        vmaDestroyBuffer(g_vk.vma, p.buffer, p.alloc);
}

static void drainAllMeshBuffers() {
    // 退出时同步释放所有网格（同一 allocator，先网格后 allocator）
    vkDeviceWaitIdle(g_vk.device);
    lock_guard<mutex> lk(g_meshMutex);
    for (auto& kv : g_activeMeshes) {
        VkMesh& op = kv.second.opaque;
        VkMesh& tr = kv.second.trans;
        if (op.buffer) { vmaDestroyBuffer(g_vk.vma, op.buffer, op.alloc); op.buffer = VK_NULL_HANDLE; op.alloc = VK_NULL_HANDLE; op.valid = false; }
        if (tr.buffer) { vmaDestroyBuffer(g_vk.vma, tr.buffer, tr.alloc); tr.buffer = VK_NULL_HANDLE; tr.alloc = VK_NULL_HANDLE; tr.valid = false; }
    }
    g_activeMeshes.clear();
    processPendingDeletes();
}

static bool rebuildChunkMesh(int cx, int cz) {
    MeshData om, tm;
    if (!buildChunkMeshCPU(cx, cz, true, om)) return false;
    if (!buildChunkMeshCPU(cx, cz, false, tm)) return false;
    lock_guard<mutex> lk(g_meshMutex);
    uint64_t key = chunkKey(cx, cz);
    VkActiveChunk& ac = g_activeMeshes[key];
    releaseMeshVK(ac.opaque);
    releaseMeshVK(ac.trans);
    uploadMeshToGPU_VK(om, ac.opaque);
    uploadMeshToGPU_VK(tm, ac.trans);
    return true;
}

static void releaseChunkMesh(int cx, int cz) {
    lock_guard<mutex> lk(g_meshMutex);
    auto it = g_activeMeshes.find(chunkKey(cx, cz));
    if (it != g_activeMeshes.end()) {
        releaseMeshVK(it->second.opaque);
        releaseMeshVK(it->second.trans);
        g_activeMeshes.erase(it);
    }
}

static void markMeshDirty(int cx, int cz) {
    lock_guard<mutex> lk(g_rebuildMutex);
    g_meshesToRebuild.insert(chunkKey(cx, cz));
}

static void markChunksDirtyAround(int wx, int wy, int wz) {
    (void)wy;
    int cx = toChunk(wx), cz = toChunk(wz);
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        int nx = cx + dx, nz = cz + dz;
        bool exists;
        {
            lock_guard<mutex> lk(G::chunksMutex);
            exists = G::chunks.count(chunkKey(nx, nz)) != 0;
        }
        if (!exists) continue;
        if (!rebuildChunkMesh(nx, nz)) markMeshDirty(nx, nz);
    }
}

// 主线程每帧调用：最多 8 个 GPU 上传（顶点格式来自 j 版网格构建器）
static void processUploads() {
    vector<UploadJob> batch;
    {
        lock_guard<mutex> lk(g_uploadMutex);
        if (g_uploadJobs.empty()) return;
        size_t n = min((size_t)MAX_MESH_REBUILD_PER_FRAME, g_uploadJobs.size());
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
        static int s_uploadCount = 0;
        if (s_uploadCount < 5) {
            LOGI("[UPLOAD] chunk(%d,%d) opaque verts=%zu idx=%zu trans verts=%zu",
                 j.cx, j.cz, j.om.vertices.size(), j.om.indices.size(), j.tm.vertices.size());
            s_uploadCount++;
        }
        lock_guard<mutex> lk(g_meshMutex);
        VkActiveChunk& ac = g_activeMeshes[chunkKey(j.cx, j.cz)];
        releaseMeshVK(ac.opaque);
        releaseMeshVK(ac.trans);
        uploadMeshToGPU_VK(j.om, ac.opaque);
        uploadMeshToGPU_VK(j.tm, ac.trans);
    }
}


// ============================================================
// Section 11. 流式管理
// ============================================================
static int g_lastPlayerCx = INT_MIN;
static int g_lastPlayerCz = INT_MIN;





static void flushDirtyChunk(int cx, int cz) {
    shared_ptr<G::ChunkData> cd;
    {
        lock_guard<mutex> lk(G::chunksMutex);
        auto it = G::chunks.find(chunkKey(cx, cz));
        if (it == G::chunks.end()) return;
        if (!it->second->dirty.exchange(false)) return;
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

static bool loadOrGenerateChunk(int cx, int cz) {
    uint64_t key = chunkKey(cx, cz);
    {
        lock_guard<mutex> lk(G::chunksMutex);
        if (G::chunks.count(key)) return false;
    }
    auto cd = make_shared<G::ChunkData>();
    bool fromDisk = Save::g_store.readChunk(cx, cz, *cd);
    if (!fromDisk) {
        generateChunkData(*cd, cx, cz);
        cd->dirty = true;    // ★ 生成线程不写盘；交给存盘队列异步落盘

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
                if (abs(cx - pcx) > UNLOAD_RADIUS || abs(cz - pcz) > UNLOAD_RADIUS)
                    toUnload.push_back(kv.first);
            }
        }
        for (uint64_t k : toUnload) {
            int cx = (int)(k >> 32);
            int cz = (int)(k & 0xFFFFFFFF);
            unloadChunk(cx, cz);
        }
    }

    struct Job { int cx, cz, d2; };
    vector<Job> jobs;
    {
        lock_guard<mutex> lk(G::chunksMutex);
        for (int dz = -LOAD_RADIUS; dz <= LOAD_RADIUS; dz++)
            for (int dx = -LOAD_RADIUS; dx <= LOAD_RADIUS; dx++) {
                int cx = pcx + dx, cz = pcz + dz;
                if (G::chunks.count(chunkKey(cx, cz))) continue;
                jobs.push_back({ cx, cz, dx * dx + dz * dz });
            }
    }
    if (!jobs.empty() || moved)
        {
            lock_guard<mutex> lk(g_jobMutex);
            for (auto& j : jobs) {
                uint64_t k = chunkKey(j.cx, j.cz);
                if (g_genQueued.count(k)) continue;
                g_genQueued.insert(k);
                g_genJobs.push_back({ j.cx, j.cz });
            }
            if (moved)   // ★ MC 票务：玩家移动后，按新位置重排整条生成队列
                sort(g_genJobs.begin(), g_genJobs.end(),
                    [pcx, pcz](const pair<int, int>& a, const pair<int, int>& b) {
                        int da = (a.first - pcx) * (a.first - pcx) + (a.second - pcz) * (a.second - pcz);
                        int db = (b.first - pcx) * (b.first - pcx) + (b.second - pcz) * (b.second - pcz);
                        return da < db;
                    });
        }
        g_jobCV.notify_all();
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



// ===== 触摸输入状态(必须定义在 updateFlyCamera 之前) =====
static int64_t nowMs() {
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now().time_since_epoch()).count();
}
struct LookTouch {
    bool active = false; int pointerId = -1;
    float sx = 0, sy = 0, lx = 0, ly = 0;
    int64_t t0 = 0; float totalDist = 0;
    bool mineFired = false; bool mineArmed = true;
};
struct Stick {
    bool active = false; int pointerId = -1;
    float ax = 0, ay = 0, dx = 0, dy = 0;
};
static LookTouch g_look;
static Stick     g_stick;
static bool    g_modeHeld = false;
static int     g_modePid = -1;
static int64_t g_modeT0 = 0;
static bool    g_modeFired = false;
static int g_jumpPid = -1;
static int g_crouchPid = -1;   // ★ 下蹲按钮触点 id

static const float STICK_R = 110.0f;   // 摇杆半径(像素)



static void updateFlyCamera(float dt) {
    float speed = FLY_SPEED * dt;
    XMFLOAT3 fwd(sinf(G::yaw)*cosf(G::pitch), sinf(G::pitch), cosf(G::yaw)*cosf(G::pitch));
    XMFLOAT3 right(cosf(G::yaw), 0, -sinf(G::yaw));

    if (G::keys['W']) { G::camPos.x += fwd.x*speed; G::camPos.y += fwd.y*speed; G::camPos.z += fwd.z*speed; }
    if (G::keys['S']) { G::camPos.x -= fwd.x*speed; G::camPos.y -= fwd.y*speed; G::camPos.z -= fwd.z*speed; }
    if (G::keys['A']) { G::camPos.x -= right.x*speed; G::camPos.z -= right.z*speed; }
    if (G::keys['D']) { G::camPos.x += right.x*speed; G::camPos.z += right.z*speed; }
    if (G::keys['Q']) G::camPos.y -= speed;
    if (G::keys['E'] || G::jumpHeld) G::camPos.y += speed; // 旁观:跳跃=上升
    if (G::crouchHeld)              G::camPos.y -= speed; // ★ 旁观:下蹲=下降

    if (g_stick.active) {
        G::camPos.x += fwd.x * (-g_stick.dy) * speed + right.x * g_stick.dx * speed;
        G::camPos.z += fwd.z * (-g_stick.dy) * speed + right.z * g_stick.dx * speed;
    }
    G::playerVel = XMFLOAT3(0, 0, 0);

    G::onGround = false;
    G::inWater = false;
}

static void updateSurvivalCamera(float dt) {
    int fx = (int)floorf(G::camPos.x);
    int fy = (int)floorf(G::camPos.y + 0.1f);
    int fz = (int)floorf(G::camPos.z);                      // ★ 补回这行
    bool ankleWater = (getBlock(fx, fy, fz) == Block::WATER);

    int fyChest = (int)floorf(G::camPos.y + 1.0f);          // ★ 胸口也检测,半身在水即算
    bool chestWater = (getBlock(fx, fyChest, fz) == Block::WATER);
    G::inWater = (ankleWater || chestWater);


    float sy = sinf(G::yaw), cy = cosf(G::yaw);
    float fwdX = sy, fwdZ = cy;
    float rightX = cy, rightZ = -sy;

    float mvX = 0, mvZ = 0;
    if (G::keys['W']) { mvX += fwdX; mvZ += fwdZ; }
    if (G::keys['S']) { mvX -= fwdX; mvZ -= fwdZ; }
    if (G::keys['A']) { mvX -= rightX; mvZ -= rightZ; }
    if (G::keys['D']) { mvX += rightX; mvZ += rightZ; }

    if (g_stick.active) {
        float sx2 = g_stick.dx, sy2 = -g_stick.dy;
        if (fabsf(sx2) < 0.12f) sx2 = 0;                    // 死区
        if (fabsf(sy2) < 0.12f) sy2 = 0;
        mvX += rightX * sx2 + fwdX * sy2;
        mvZ += rightZ * sx2 + fwdZ * sy2;
    }
    bool sprinting = (G::keys[VK_SHIFT] != 0);
    float stickMag = sqrtf(g_stick.dx * g_stick.dx + g_stick.dy * g_stick.dy);
    if (g_stick.active && stickMag >= 0.95f) sprinting = true;   // 推到底=冲刺
    float speed = sprinting ? WALK_SPEED_SPRINT : WALK_SPEED_NORMAL;
    if (G::crouchHeld) speed *= 0.4f;   // ★ 下蹲=潜行,40% 速度
    float mvLen = sqrtf(mvX * mvX + mvZ * mvZ);

    if (mvLen > 1.0f) { mvX /= mvLen; mvZ /= mvLen; mvLen = 1.0f; }
    if (mvLen > 1e-4f) { mvX *= speed; mvZ *= speed; }           // 半程=半速
    else { mvX = mvZ = 0; }


    //bool spaceDown = G::keys[VK_SPACE] != 0;

    bool spaceDown = (G::keys[VK_SPACE] != 0) || G::jumpHeld;   // ★ 触摸跳跃并入
    if (G::inWater) {
        G::onGround = false;
        if (spaceDown) {
            G::playerVel.y = SWIM_UP_SPEED;
            int hy = (int)floorf(G::camPos.y + PLAYER_EYE_HEIGHT);
            if (getBlock(fx, hy, fz) != Block::WATER && !G::waterJumpLatch) {
                G::playerVel.y = JUMP_VELOCITY * 0.8f;   // 头出水:跳出脉冲助上岸
                G::waterJumpLatch = true;
            }
        }
        else {
            G::playerVel.y = SWIM_DOWN_SPEED * 0.5f;     // 松手缓沉
            G::waterJumpLatch = false;
        }
    }

    else {
            G::playerVel.y -= GRAVITY * dt;
            if (G::playerVel.y < TERMINAL_VELOCITY)
                G::playerVel.y = TERMINAL_VELOCITY;
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
// ============================= 控制台 =============================
static bool                 g_consoleOpen = false;
static std::string          g_cmdBuf;
static std::vector<std::string> g_cmdLog;      // 回显最近 8 条



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
    cd->lightDirty = true;
    if (b != Block::AIR) {
        uint16_t& ct = cd->colTop[chunkBiomeIndex(lx, lz)];
        if ((int)(RY0 + ct) < y) ct = (uint16_t)(y - RY0);
    }

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
            for (int ccx = toChunk(x); ccx <= toChunk(x + dx); ccx++)
                for (int ccz = toChunk(z); ccz <= toChunk(z + dz); ccz++)
                    enqueueMeshJob(ccx, ccz, true);
        }
        else g_cmdLog.push_back("usage: /fill x y z dx dy dz [AIR|STONE|...]");
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
    g_cmdLog.push_back("unknown cmd");
}
static bool initVulkan(ANativeWindow* window) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "VoxelVulkan";
    app.apiVersion = VK_API_VERSION_1_1;

    // Android 上没有 validation layer，直接空
    const char* exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        "VK_KHR_android_surface",
    };
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledLayerCount = 0;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = exts;

    if (vkCreateInstance(&ici, nullptr, &g_vk.instance) != VK_SUCCESS) {
        LOGE("[VK] vkCreateInstance failed");
        return false;
    }

    VkAndroidSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    sci.window = window;
    if (vkCreateAndroidSurfaceKHR(g_vk.instance, &sci, nullptr, &g_vk.surface) != VK_SUCCESS) {
        LOGE("[VK] vkCreateAndroidSurfaceKHR failed");
        return false;
    }

    uint32_t devCount = 0;
    vkEnumeratePhysicalDevices(g_vk.instance, &devCount, nullptr);
    if (devCount == 0) { LOGE("[VK] No physical device"); return false; }
    vector<VkPhysicalDevice> devs(devCount);
    vkEnumeratePhysicalDevices(g_vk.instance, &devCount, devs.data());
    g_vk.physical = devs[0];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(g_vk.physical, &props);
    LOGI("[VK] GPU: %s", props.deviceName);

    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.physical, &qfCount, nullptr);
    vector<VkQueueFamilyProperties> qfs(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.physical, &qfCount, qfs.data());

    g_vk.gfxFamily = UINT32_MAX;
    g_vk.presentFamily = UINT32_MAX;
    for (uint32_t i = 0; i < qfCount; i++) {
        if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && g_vk.gfxFamily == UINT32_MAX)
            g_vk.gfxFamily = i;
        VkBool32 support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(g_vk.physical, i, g_vk.surface, &support);
        if (support && g_vk.presentFamily == UINT32_MAX)
            g_vk.presentFamily = i;
    }

    float priority = 1.0f;
    vector<uint32_t> fams = { g_vk.gfxFamily, g_vk.presentFamily };
    sort(fams.begin(), fams.end());
    fams.erase(unique(fams.begin(), fams.end()), fams.end());

    vector<VkDeviceQueueCreateInfo> qcis;
    for (uint32_t f : fams) {
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = f;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        qcis.push_back(qci);
    }

    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = (uint32_t)qcis.size();
    dci.pQueueCreateInfos = qcis.data();
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;

    if (vkCreateDevice(g_vk.physical, &dci, nullptr, &g_vk.device) != VK_SUCCESS) {
        LOGE("[VK] vkCreateDevice failed");
        return false;
    }
    vkGetDeviceQueue(g_vk.device, g_vk.gfxFamily, 0, &g_vk.gfxQueue);
    vkGetDeviceQueue(g_vk.device, g_vk.presentFamily, 0, &g_vk.presentQueue);

    VmaAllocatorCreateInfo ai = {};
    ai.physicalDevice = g_vk.physical;
    ai.device = g_vk.device;
    ai.instance = g_vk.instance;
    ai.vulkanApiVersion = VK_API_VERSION_1_1;
    vmaCreateAllocator(&ai, &g_vk.vma);

    if (!createSwapchain())          return false;
    if (!createRenderPass())         return false;
    if (!createDescriptorSetLayout())return false;
    if (!createGraphicsPipeline())   return false;
    if (!createCommandPool())        return false;
    if (!createDepthResources())     return false;
    if (!createFramebuffers())       return false;
    if (!createCameraUBO())          return false;
    if (!createDescriptorPoolAndSet())return false;
    if (!createParticleVB())         return false;
    if (!createTextureArray())       return false;

    if (!loadFontFromAssets())       return false;
    if (!createHudPipeline())        return false;
    if (!createHudTexture(g_width, g_height)) return false;

    if (!createSyncObjects())        return false;

    LOGI("[VK] Init done");
    return true;
}

static bool createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vk.physical, g_vk.surface, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.physical, g_vk.surface, &fmtCount, nullptr);
    vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vk.physical, g_vk.surface, &fmtCount, fmts.data());

    VkSurfaceFormatKHR chosen = fmts[0];
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f; break;
        }
    }
    g_vk.swapFormat = chosen.format;

    if (caps.currentExtent.width != UINT32_MAX) {
        g_vk.swapExtent = caps.currentExtent;
    } else {
        g_vk.swapExtent = { (uint32_t)g_width, (uint32_t)g_height };
    }
    g_width  = (int)g_vk.swapExtent.width;
    g_height = (int)g_vk.swapExtent.height;

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
        imgCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR swci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swci.surface = g_vk.surface;
    swci.minImageCount = imgCount;
    swci.imageFormat = chosen.format;
    swci.imageColorSpace = chosen.colorSpace;
    swci.imageExtent = g_vk.swapExtent;
    swci.imageArrayLayers = 1;
    swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t famIdx[2] = { g_vk.gfxFamily, g_vk.presentFamily };
    if (g_vk.gfxFamily != g_vk.presentFamily) {
        swci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swci.queueFamilyIndexCount = 2;
        swci.pQueueFamilyIndices = famIdx;
    } else {
        swci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    // ★ 我们在 shader 里不预旋转，所以强制 IDENTITY，
    //    由 compositor 负责把画面旋转到物理屏幕方向。
    if (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        swci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        LOGI("[VK] using preTransform = IDENTITY");
    } else {
        swci.preTransform = caps.currentTransform;
        LOGI("[VK] IDENTITY not supported, fallback to currentTransform");
    }
    swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swci.clipped = VK_TRUE;

    vkCreateSwapchainKHR(g_vk.device, &swci, nullptr, &g_vk.swapchain);

    uint32_t sc = 0;
    vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &sc, nullptr);
    g_vk.swapImages.resize(sc);
    vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &sc, g_vk.swapImages.data());

    g_vk.swapViews.resize(sc);
    for (uint32_t i = 0; i < sc; i++) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = g_vk.swapImages[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = g_vk.swapFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(g_vk.device, &vci, nullptr, &g_vk.swapViews[i]);
    }
    LOGI("[VK] Swapchain: %ux%u, %u images",
         g_vk.swapExtent.width, g_vk.swapExtent.height, sc);
    return true;
}
// ============================================================
// 窗口生命周期修复：surface/swapchain 可安全销毁重建，
// device/pipeline/VMA/网格 全程只建一次，彻底避免
// "Pointer doesn't belong to this memory pool" 断言闪退。
// ============================================================

static void destroyRenderDoneSemaphores() {
    for (auto s : g_vk.renderDone)
        if (s) vkDestroySemaphore(g_vk.device, s, nullptr);
    g_vk.renderDone.clear();
}

static void createRenderDoneSemaphores() {
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    g_vk.renderDone.resize(g_vk.swapImages.size());
    for (size_t i = 0; i < g_vk.swapImages.size(); i++)
        vkCreateSemaphore(g_vk.device, &si, nullptr, &g_vk.renderDone[i]);
}

// 销毁与 surface/swapchain 绑定的所有资源；destroySurface=true 时连 surface 一起销毁
//（窗口重建后旧 surface 已失效，必须销毁并按新 ANativeWindow 重建）
static void destroySwapchainFamily(bool destroySurface) {
    if (g_vk.device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(g_vk.device);

    for (auto fb : g_vk.swapFramebuffers)
        if (fb) vkDestroyFramebuffer(g_vk.device, fb, nullptr);
    g_vk.swapFramebuffers.clear();

    for (auto v : g_vk.swapViews)
        if (v) vkDestroyImageView(g_vk.device, v, nullptr);
    g_vk.swapViews.clear();

    if (g_vk.depthView)   { vkDestroyImageView(g_vk.device, g_vk.depthView, nullptr);   g_vk.depthView = VK_NULL_HANDLE; }
    if (g_vk.depthImage)  { vmaDestroyImage(g_vk.vma, g_vk.depthImage, g_vk.depthAlloc); g_vk.depthImage = VK_NULL_HANDLE; g_vk.depthAlloc = VK_NULL_HANDLE; }

    destroyRenderDoneSemaphores();

    if (g_vk.swapchain)   { vkDestroySwapchainKHR(g_vk.device, g_vk.swapchain, nullptr); g_vk.swapchain = VK_NULL_HANDLE; }
    if (destroySurface && g_vk.surface) {
        vkDestroySurfaceKHR(g_vk.instance, g_vk.surface, nullptr);
        g_vk.surface = VK_NULL_HANDLE;
    }
}

// 用 (可能全新的) ANativeWindow 建立 surface + swapchain 全家桶
static bool createSurfaceAndSwapchain(ANativeWindow* window) {
    if (!window || g_vk.device == VK_NULL_HANDLE) return false;
    if (g_vk.surface == VK_NULL_HANDLE) {
        VkAndroidSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
        sci.window = window;
        if (vkCreateAndroidSurfaceKHR(g_vk.instance, &sci, nullptr, &g_vk.surface) != VK_SUCCESS) {
            LOGE("[VK] vkCreateAndroidSurfaceKHR failed (recreate)");
            return false;
        }
    }
    if (!createSwapchain())         return false;
    if (!createDepthResources())    return false;
    if (!createFramebuffers())      return false;
    createRenderDoneSemaphores();
    recreateHud((int)g_vk.swapExtent.width, (int)g_vk.swapExtent.height);
    LOGI("[VK] Swapchain created: %ux%u (%zu images)",
         g_vk.swapExtent.width, g_vk.swapExtent.height, g_vk.swapImages.size());
    return true;
}

// 同一 window 内 resize 触发的重建（不销毁 surface）
static void recreateSwapchain() {
    if (!g_app || !g_app->window) return;
    int w = ANativeWindow_getWidth(g_app->window);
    int h = ANativeWindow_getHeight(g_app->window);
    if (w <= 0 || h <= 0) return;
    destroySwapchainFamily(false);
    createSurfaceAndSwapchain(g_app->window);
}

static bool createSyncObjects() {
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateSemaphore(g_vk.device, &si, nullptr, &g_vk.imageAvail[i]);
        vkCreateFence(g_vk.device, &fi, nullptr, &g_vk.inFlight[i]);
    }
    createRenderDoneSemaphores();
    return true;
}

// 完全退出时销毁全部 Vulkan 资源（顺序关键：网格/延迟队列 → HUD → 其它 → VMA 最后）
static void cleanupVulkan() {
    if (g_vk.device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(g_vk.device);

    drainAllMeshBuffers();           // 所有 VMA 网格句柄先释放

    if (g_hud.stagingBuffer) { vmaDestroyBuffer(g_vk.vma, g_hud.stagingBuffer, g_hud.stagingAlloc); g_hud.stagingBuffer = VK_NULL_HANDLE; g_hud.stagingAlloc = VK_NULL_HANDLE; g_hud.stagingMapped = nullptr; g_hud.stagingSize = 0; }
    if (g_hud.sampler)   { vkDestroySampler(g_vk.device, g_hud.sampler, nullptr);   g_hud.sampler = VK_NULL_HANDLE; }
    if (g_hud.imageView) { vkDestroyImageView(g_vk.device, g_hud.imageView, nullptr); g_hud.imageView = VK_NULL_HANDLE; }
    if (g_hud.image)     { vmaDestroyImage(g_vk.vma, g_hud.image, g_hud.imageAlloc); g_hud.image = VK_NULL_HANDLE; g_hud.imageAlloc = VK_NULL_HANDLE; }
    if (g_hud.descSet)   { vkFreeDescriptorSets(g_vk.device, g_hud.descPool, 1, &g_hud.descSet); g_hud.descSet = VK_NULL_HANDLE; }
    if (g_hud.descPool)  { vkDestroyDescriptorPool(g_vk.device, g_hud.descPool, nullptr); g_hud.descPool = VK_NULL_HANDLE; }
    if (g_hud.descSetLayout) { vkDestroyDescriptorSetLayout(g_vk.device, g_hud.descSetLayout, nullptr); g_hud.descSetLayout = VK_NULL_HANDLE; }
    if (g_hud.pipeline)  { vkDestroyPipeline(g_vk.device, g_hud.pipeline, nullptr); g_hud.pipeline = VK_NULL_HANDLE; }
    if (g_hud.pipelineLayout) { vkDestroyPipelineLayout(g_vk.device, g_hud.pipelineLayout, nullptr); g_hud.pipelineLayout = VK_NULL_HANDLE; }

    destroySwapchainFamily(true);

    if (g_vk.particleVB) { vmaDestroyBuffer(g_vk.vma, g_vk.particleVB, g_vk.particleVBAlloc); g_vk.particleVB = VK_NULL_HANDLE; g_vk.particleVBAlloc = VK_NULL_HANDLE; g_vk.particleVBMapped = nullptr; }
    if (g_vk.camUBO)     { vmaDestroyBuffer(g_vk.vma, g_vk.camUBO, g_vk.camUBOAlloc); g_vk.camUBO = VK_NULL_HANDLE; g_vk.camUBOAlloc = VK_NULL_HANDLE; g_vk.camUBOMapped = nullptr; }
    if (g_vk.texArrayImage) { vmaDestroyImage(g_vk.vma, g_vk.texArrayImage, g_vk.texArrayAlloc); g_vk.texArrayImage = VK_NULL_HANDLE; g_vk.texArrayAlloc = VK_NULL_HANDLE; }
    if (g_vk.texArrayView)  { vkDestroyImageView(g_vk.device, g_vk.texArrayView, nullptr); g_vk.texArrayView = VK_NULL_HANDLE; }
    if (g_vk.texSampler)    { vkDestroySampler(g_vk.device, g_vk.texSampler, nullptr); g_vk.texSampler = VK_NULL_HANDLE; }
    if (g_vk.descSet)       { vkFreeDescriptorSets(g_vk.device, g_vk.descPool, 1, &g_vk.descSet); g_vk.descSet = VK_NULL_HANDLE; }
    if (g_vk.descPool)      { vkDestroyDescriptorPool(g_vk.device, g_vk.descPool, nullptr); g_vk.descPool = VK_NULL_HANDLE; }
    if (g_vk.descSetLayout) { vkDestroyDescriptorSetLayout(g_vk.device, g_vk.descSetLayout, nullptr); g_vk.descSetLayout = VK_NULL_HANDLE; }
    if (g_vk.voxelPipeline)     { vkDestroyPipeline(g_vk.device, g_vk.voxelPipeline, nullptr); g_vk.voxelPipeline = VK_NULL_HANDLE; }
    if (g_vk.voxelPipelineTrans){ vkDestroyPipeline(g_vk.device, g_vk.voxelPipelineTrans, nullptr); g_vk.voxelPipelineTrans = VK_NULL_HANDLE; }
    if (g_vk.particlePipeline)  { vkDestroyPipeline(g_vk.device, g_vk.particlePipeline, nullptr); g_vk.particlePipeline = VK_NULL_HANDLE; }
    if (g_vk.pipelineLayout)    { vkDestroyPipelineLayout(g_vk.device, g_vk.pipelineLayout, nullptr); g_vk.pipelineLayout = VK_NULL_HANDLE; }
    if (g_vk.renderPass)        { vkDestroyRenderPass(g_vk.device, g_vk.renderPass, nullptr); g_vk.renderPass = VK_NULL_HANDLE; }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (g_vk.imageAvail[i]) vkDestroySemaphore(g_vk.device, g_vk.imageAvail[i], nullptr);
        if (g_vk.inFlight[i])   vkDestroyFence(g_vk.device, g_vk.inFlight[i], nullptr);
        g_vk.imageAvail[i] = VK_NULL_HANDLE;
        g_vk.inFlight[i]   = VK_NULL_HANDLE;
    }
    if (g_vk.cmdPool) { vkDestroyCommandPool(g_vk.device, g_vk.cmdPool, nullptr); g_vk.cmdPool = VK_NULL_HANDLE; }

    if (g_vk.vma)    { vmaDestroyAllocator(g_vk.vma); g_vk.vma = VK_NULL_HANDLE; }   // VMA 必须最后销毁
    if (g_vk.device) { vkDestroyDevice(g_vk.device, nullptr); g_vk.device = VK_NULL_HANDLE; }
    if (g_vk.instance) { vkDestroyInstance(g_vk.instance, nullptr); g_vk.instance = VK_NULL_HANDLE; }
    LOGI("[VK] Cleanup done");
}


static VkFormat findDepthFormat() {
    VkFormat candidates[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT };
    for (auto f : candidates) {
        VkFormatProperties p;
        vkGetPhysicalDeviceFormatProperties(g_vk.physical, f, &p);
        if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return f;
    }
    return VK_FORMAT_D32_SFLOAT;
}

static bool createRenderPass() {
    VkAttachmentDescription color{};
    color.format = g_vk.swapFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format = findDepthFormat();
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription atts[] = { color, depth };
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 2;
    rpci.pAttachments = atts;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    vkCreateRenderPass(g_vk.device, &rpci, nullptr, &g_vk.renderPass);
    return true;
}

static bool createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding bindings[2] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = 2;
    ci.pBindings = bindings;
    vkCreateDescriptorSetLayout(g_vk.device, &ci, nullptr, &g_vk.descSetLayout);
    return true;
}

static bool createGraphicsPipeline() {
    VkShaderModule vs = createShaderModuleFromSPV(kSPV_Voxel2Vert, sizeof(kSPV_Voxel2Vert)/sizeof(uint32_t));
    VkShaderModule fs = createShaderModuleFromSPV(kSPV_Voxel2Frag, sizeof(kSPV_Voxel2Frag)/sizeof(uint32_t));

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs; stages[1].pName = "main";

    VkVertexInputBindingDescription bind{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[7] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
        {3, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, uv)},
        {4, 0, VK_FORMAT_R32_SFLOAT,       offsetof(Vertex, texLayer)},
        {5, 0, VK_FORMAT_R32_SFLOAT,       offsetof(Vertex, shade)},
        {6, 0, VK_FORMAT_R32_SFLOAT,       offsetof(Vertex, light)},
    };

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 7;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    cba.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dyn;

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &g_vk.descSetLayout;
    vkCreatePipelineLayout(g_vk.device, &plci, nullptr, &g_vk.pipelineLayout);

    VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pDepthStencilState = &ds;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &dynState;
    gpci.layout = g_vk.pipelineLayout;
    gpci.renderPass = g_vk.renderPass;
    gpci.subpass = 0;

    VkResult pr = vkCreateGraphicsPipelines(g_vk.device, VK_NULL_HANDLE, 1, &gpci, nullptr, &g_vk.voxelPipeline);
    if (pr != VK_SUCCESS) LOGE("[VK] voxelPipeline failed (%d)", (int)pr);

    ds.depthWriteEnable = VK_FALSE;
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;

    pr = vkCreateGraphicsPipelines(g_vk.device, VK_NULL_HANDLE, 1, &gpci, nullptr, &g_vk.voxelPipelineTrans);
    if (pr != VK_SUCCESS) LOGE("[VK] voxelPipelineTrans failed (%d)", (int)pr);

    vkDestroyShaderModule(g_vk.device, vs, nullptr);
    vkDestroyShaderModule(g_vk.device, fs, nullptr);

    // 粒子管线
    VkShaderModule pvs = createShaderModuleFromSPV(kSPV_ParticleVert, sizeof(kSPV_ParticleVert)/sizeof(uint32_t));
    VkShaderModule pfs = createShaderModuleFromSPV(kSPV_ParticleFrag, sizeof(kSPV_ParticleFrag)/sizeof(uint32_t));

    VkPipelineShaderStageCreateInfo pstages[2] = {};
    pstages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pstages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    pstages[0].module = pvs; pstages[0].pName = "main";
    pstages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pstages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    pstages[1].module = pfs; pstages[1].pName = "main";

    VkVertexInputBindingDescription pbind{0, sizeof(ParticleVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription pattrs[5] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ParticleVertex, center)},
        {1, 0, VK_FORMAT_R32_SFLOAT,       offsetof(ParticleVertex, size)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(ParticleVertex, corner)},
        {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ParticleVertex, color)},
        {4, 0, VK_FORMAT_R32_SFLOAT,       offsetof(ParticleVertex, alpha)},
    };
    vi.pVertexBindingDescriptions = &pbind;
    vi.vertexAttributeDescriptionCount = 5;
    vi.pVertexAttributeDescriptions = pattrs;

    rs.cullMode = VK_CULL_MODE_NONE;
    ds.depthWriteEnable = VK_FALSE;
    gpci.pStages = pstages;
    pr = vkCreateGraphicsPipelines(g_vk.device, VK_NULL_HANDLE, 1, &gpci, nullptr, &g_vk.particlePipeline);
    if (pr != VK_SUCCESS) LOGE("[VK] particlePipeline failed (%d)", (int)pr);

    vkDestroyShaderModule(g_vk.device, pvs, nullptr);
    vkDestroyShaderModule(g_vk.device, pfs, nullptr);

    return true;
}
static bool createFramebuffers() {
    g_vk.swapFramebuffers.resize(g_vk.swapViews.size());
    for (size_t i = 0; i < g_vk.swapViews.size(); i++) {
        VkImageView atts[] = { g_vk.swapViews[i], g_vk.depthView };
        VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fci.renderPass = g_vk.renderPass;
        fci.attachmentCount = 2;
        fci.pAttachments = atts;
        fci.width = g_vk.swapExtent.width;
        fci.height = g_vk.swapExtent.height;
        fci.layers = 1;
        vkCreateFramebuffer(g_vk.device, &fci, nullptr, &g_vk.swapFramebuffers[i]);
    }
    return true;
}

static bool createCommandPool() {
    VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = g_vk.gfxFamily;
    vkCreateCommandPool(g_vk.device, &ci, nullptr, &g_vk.cmdPool);

    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = g_vk.cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    vkAllocateCommandBuffers(g_vk.device, &ai, g_vk.cmdBuf);
    return true;
}

static bool createDepthResources() {
    VkFormat df = findDepthFormat();
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.extent = { g_vk.swapExtent.width, g_vk.swapExtent.height, 1 };
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.format = df;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaCreateImage(g_vk.vma, &ci, &ai, &g_vk.depthImage, &g_vk.depthAlloc, nullptr);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = g_vk.depthImage;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = df;
    vci.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    vkCreateImageView(g_vk.device, &vci, nullptr, &g_vk.depthView);
    return true;
}

static bool createCameraUBO() {
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = sizeof(CameraUBO);
    ci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo out;
    vmaCreateBuffer(g_vk.vma, &ci, &ai, &g_vk.camUBO, &g_vk.camUBOAlloc, &out);
    g_vk.camUBOMapped = out.pMappedData;
    return true;
}

static bool createDescriptorPoolAndSet() {
    VkDescriptorPoolSize sizes[2] = {};
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = 1;
    sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 1;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = sizes;
    vkCreateDescriptorPool(g_vk.device, &pci, nullptr, &g_vk.descPool);

    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = g_vk.descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &g_vk.descSetLayout;
    vkAllocateDescriptorSets(g_vk.device, &ai, &g_vk.descSet);

    VkDescriptorBufferInfo bi{g_vk.camUBO, 0, sizeof(CameraUBO)};

    VkWriteDescriptorSet w[2] = {};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = g_vk.descSet;
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo = &bi;

    vkUpdateDescriptorSets(g_vk.device, 1, w, 0, nullptr);
    return true;
}

static bool createParticleVB() {
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = MAX_PARTICLES * 4 * sizeof(ParticleVertex);
    ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo out;
    vmaCreateBuffer(g_vk.vma, &ci, &ai, &g_vk.particleVB, &g_vk.particleVBAlloc, &out);
    g_vk.particleVBMapped = out.pMappedData;
    return true;
}
static bool uploadTextureArray(const vector<vector<uint8_t>>& texData, int tw, int th) {
    uint32_t layers = (uint32_t)texData.size();
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.extent = { (uint32_t)tw, (uint32_t)th, 1 };
    ci.mipLevels = 1;
    ci.arrayLayers = layers;
    ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaCreateImage(g_vk.vma, &ci, &ai, &g_vk.texArrayImage, &g_vk.texArrayAlloc, nullptr);

    VkDeviceSize total = (VkDeviceSize)tw * th * 4 * layers;
    VkBuffer staging;
    VmaAllocation stageAlloc;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = total;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo bai{};
    bai.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    bai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo sout;
    vmaCreateBuffer(g_vk.vma, &bci, &bai, &staging, &stageAlloc, &sout);

    for (uint32_t i = 0; i < layers; i++) {
        memcpy((uint8_t*)sout.pMappedData + (size_t)i * tw * th * 4,
               texData[i].data(), (size_t)tw * th * 4);
    }
    vmaFlushAllocation(g_vk.vma, stageAlloc, 0, total);

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = g_vk.cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(g_vk.device, &cbai, &cb);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = g_vk.texArrayImage;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &b);

    for (uint32_t i = 0; i < layers; i++) {
        VkBufferImageCopy r = {};
        r.bufferOffset = (VkDeviceSize)i * tw * th * 4;
        r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, i, 1};
        r.imageOffset = {0, 0, 0};
        r.imageExtent = { (uint32_t)tw, (uint32_t)th, 1 };
        vkCmdCopyBufferToImage(cb, staging, g_vk.texArrayImage,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    }

    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &b);

    vkEndCommandBuffer(cb);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(g_vk.gfxQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_vk.gfxQueue);

    vkFreeCommandBuffers(g_vk.device, g_vk.cmdPool, 1, &cb);
    vmaDestroyBuffer(g_vk.vma, staging, stageAlloc);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = g_vk.texArrayImage;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
    vkCreateImageView(g_vk.device, &vci, nullptr, &g_vk.texArrayView);

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 0.0f;
    vkCreateSampler(g_vk.device, &sci, nullptr, &g_vk.texSampler);

    g_vk.texLayerCount = layers;

    VkDescriptorImageInfo ii{};
    ii.sampler = g_vk.texSampler;
    ii.imageView = g_vk.texArrayView;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = g_vk.descSet;
    w.dstBinding = 1;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(g_vk.device, 1, &w, 0, nullptr);

    LOGI("[VK] Texture array: %u layers %dx%d", layers, tw, th);
    return true;
}

// 从 assets 读 PNG（stb_image 解码）
static bool decodeAllTextures(vector<vector<uint8_t>>& out, int& tw, int& th) {
    static const char* names[TEX_COUNT] = {
        "stone", "shenbanstone", "nitu", "shazi", "caoshang",
        "mutoushang", "mutouce", "tuff", "calcite", "andesite", "diorite", "granite",
        "podzol_top", "podzol_side", "mycelium", "mud", "red_sand", "gravel",
        "clay", "sandstone", "red_sandstone", "terracotta", "planks",
        "coal_ore", "iron_ore", "copper_ore", "gold_ore", "redstone_ore",
        "lapis_ore", "diamond_ore", "emerald_ore"
    };

    AAssetManager* mgr = g_app->activity->assetManager;
    out.resize(TEX_COUNT);

    for (int i = 0; i < TEX_COUNT; i++) {
        string path = string("tex/") + names[i] + ".png";
        AAsset* asset = AAssetManager_open(mgr, path.c_str(), AASSET_MODE_BUFFER);
        if (!asset) {
            LOGE("[TEX] Missing asset: %s", path.c_str());
            return false;
        }
        size_t size = AAsset_getLength(asset);
        vector<uint8_t> raw(size);
        AAsset_read(asset, raw.data(), size);
        AAsset_close(asset);

        int w = 0, h = 0, n = 0;
        stbi_uc* pix = stbi_load_from_memory(raw.data(), (int)raw.size(), &w, &h, &n, 4);
        if (!pix) { LOGE("[TEX] decode failed: %s", path.c_str()); return false; }
        if (tw == 0) { tw = w; th = h; }
        out[i].assign(pix, pix + (size_t)w * h * 4);
        stbi_image_free(pix);
    }
    return true;
}

static bool createTextureArray() {
    vector<vector<uint8_t>> data;
    int tw = 0, th = 0;
    if (!decodeAllTextures(data, tw, th)) return false;
    return uploadTextureArray(data, tw, th);
}
static bool loadFontFromAssets() {
    AAssetManager* mgr = g_app->activity->assetManager;
    AAsset* asset = AAssetManager_open(mgr, "font/font.ttf", AASSET_MODE_BUFFER);
    if (!asset) { LOGE("[HUD] font.ttf not found"); return false; }
    size_t size = AAsset_getLength(asset);
    g_hud.fontData.resize(size);
    AAsset_read(asset, g_hud.fontData.data(), size);
    AAsset_close(asset);

    if (!stbtt_InitFont(&g_hud.fontInfo, g_hud.fontData.data(),
                        stbtt_GetFontOffsetForIndex(g_hud.fontData.data(), 0))) {
        LOGE("[HUD] stbtt_InitFont failed");
        return false;
    }
    g_hud.fontScale = stbtt_ScaleForPixelHeight(&g_hud.fontInfo, g_hud.fontSize);
    g_hud.fontLoaded = true;
    return true;
}
// 把 UTF-8 解码成 codepoint 序列
static vector<int> utf8ToCodepoints(const string& s) {
    vector<int> cps;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i];
        int cp = 0; int extra = 0;
        if (c < 0x80)      { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { i++; continue; }
        i++;
        for (int k = 0; k < extra && i < s.size(); k++, i++)
            cp = (cp << 6) | (s[i] & 0x3F);
        cps.push_back(cp);
    }
    return cps;
}

// 在 g_hud.pixels 上绘制字符串；颜色 0xAABBGGRR（BGRA）
static void hudDrawString(int x, int y, const string& text, uint32_t color) {
    if (!g_hud.fontLoaded) return;
    int w = g_hud.width, h = g_hud.height;
    int px = x, py = y;
    float scale = g_hud.fontScale;
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&g_hud.fontInfo, &ascent, &descent, &lineGap);
    int baseline = (int)(ascent * scale);
    py += baseline;

    for (int cp : utf8ToCodepoints(text)) {
        if (cp == '\n') {
            px = x;
            py += (int)((ascent - descent + lineGap) * scale);
            continue;
        }
        int gw, gh, xoff, yoff;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(
            &g_hud.fontInfo, 0, scale, cp, &gw, &gh, &xoff, &yoff);

        for (int gy = 0; gy < gh; gy++) {
            for (int gx = 0; gx < gw; gx++) {
                int sx = px + xoff + gx;
                int sy = py + yoff + gy;
                if (sx < 0 || sx >= w || sy < 0 || sy >= h) continue;
                int a = bitmap[gy * gw + gx];
                if (a == 0) continue;
                uint32_t old = g_hud.pixels[sy * w + sx];
                // 简单 alpha 混合（未做前置乘算，效果够用）
                int sr = (color >> 16) & 0xFF;
                int sg = (color >> 8)  & 0xFF;
                int sb =  color        & 0xFF;
                int dr = (old >> 16) & 0xFF;
                int dg = (old >> 8)  & 0xFF;
                int db =  old        & 0xFF;
                int nr = (sr * a + dr * (255 - a)) / 255;
                int ng = (sg * a + dg * (255 - a)) / 255;
                int nb = (sb * a + db * (255 - a)) / 255;
                int na = max((int)(old >> 24), a);
                g_hud.pixels[sy * w + sx] = (na << 24) | (nr << 16) | (ng << 8) | nb;
            }
        }
        stbtt_FreeBitmap(bitmap, nullptr);

        int adv, lsb;
        stbtt_GetCodepointHMetrics(&g_hud.fontInfo, cp, &adv, &lsb);
        px += (int)(adv * scale);
    }
}

static bool createHudPipeline() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 1;
    dslci.pBindings = &binding;
    vkCreateDescriptorSetLayout(g_vk.device, &dslci, nullptr, &g_hud.descSetLayout);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &poolSize;
    vkCreateDescriptorPool(g_vk.device, &dpci, nullptr, &g_hud.descPool);

    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = g_hud.descPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &g_hud.descSetLayout;
    vkAllocateDescriptorSets(g_vk.device, &dsai, &g_hud.descSet);

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &g_hud.descSetLayout;
    vkCreatePipelineLayout(g_vk.device, &plci, nullptr, &g_hud.pipelineLayout);

    VkShaderModule vs = createShaderModuleFromSPV(kSPV_HudVert, sizeof(kSPV_HudVert)/sizeof(uint32_t));
    VkShaderModule fs = createShaderModuleFromSPV(kSPV_HudFrag, sizeof(kSPV_HudFrag)/sizeof(uint32_t));

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs; stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo viHud{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vps.viewportCount = 1; vps.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyns{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyns.dynamicStateCount = 2;
    dyns.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &viHud;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vps;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pDepthStencilState = &ds;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &dyns;
    gpci.layout = g_hud.pipelineLayout;
    gpci.renderPass = g_vk.renderPass;
    gpci.subpass = 0;

    VkResult r = vkCreateGraphicsPipelines(g_vk.device, VK_NULL_HANDLE, 1, &gpci, nullptr, &g_hud.pipeline);
    vkDestroyShaderModule(g_vk.device, vs, nullptr);
    vkDestroyShaderModule(g_vk.device, fs, nullptr);
    return r == VK_SUCCESS;
}

static bool createHudTexture(int w, int h) {
    g_hud.width = w;
    g_hud.height = h;
    g_hud.pixels.assign((size_t)w * h, 0);

    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.extent = {(uint32_t)w, (uint32_t)h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.format = VK_FORMAT_B8G8R8A8_UNORM;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaCreateImage(g_vk.vma, &ci, &ai, &g_hud.image, &g_hud.imageAlloc, nullptr);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = g_hud.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_B8G8R8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(g_vk.device, &vci, nullptr, &g_hud.imageView);

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(g_vk.device, &sci, nullptr, &g_hud.sampler);

    VkDeviceSize sz = (VkDeviceSize)w * h * 4;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = sz;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo bai{};
    bai.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    bai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo out;
    vmaCreateBuffer(g_vk.vma, &bci, &bai, &g_hud.stagingBuffer, &g_hud.stagingAlloc, &out);
    g_hud.stagingMapped = out.pMappedData;
    g_hud.stagingSize = sz;

    VkDescriptorImageInfo ii{};
    ii.sampler = g_hud.sampler;
    ii.imageView = g_hud.imageView;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet wds{};
    wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds.dstSet = g_hud.descSet;
    wds.dstBinding = 0;
    wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wds.pImageInfo = &ii;
    vkUpdateDescriptorSets(g_vk.device, 1, &wds, 0, nullptr);

    return true;
}

static void hudFillCircle(int cx, int cy, int r, uint32_t color) {
    for (int dy = -r; dy <= r; dy++) {
        int half = (int)sqrtf((float)(r * r - dy * dy));
        for (int x = cx - half; x <= cx + half; x++) {
            int sx = x, sy = cy + dy;
            if (sx < 0 || sx >= g_hud.width || sy < 0 || sy >= g_hud.height) continue;
            g_hud.pixels[sy * g_hud.width + sx] = color;
        }
    }
}
static void hudRingCircle(int cx, int cy, int r, uint32_t color) {
    for (int a = 0; a < 720; a++) {
        float t = a * 3.14159265f / 360.0f;
        int x = cx + (int)(cosf(t) * r), y = cy + (int)(sinf(t) * r);
        if (x >= 0 && x < g_hud.width && y >= 0 && y < g_hud.height)
            g_hud.pixels[y * g_hud.width + x] = color;
    }
}



// ★ 触摸 UI 布局唯一来源:drawHudText(绘制)与 handleMotionEvent(命中)共用
static void uiLayout(int w, int h,
    int& jx, int& jy, int& jr,      // JUMP
    int& crx, int& cry, int& crr,   // CROUCH
    int& bx, int& by, int& br,      // 方块切换
    int& mx0, int& my0, int& mx1, int& my1) // MODE 矩形
{
    float mh = (float)((w < h) ? w : h);
    // JUMP:加大(0.11→0.12),上移给下蹲让位
    jx = (int)(w * 0.86f); jy = (int)(h * 0.70f); jr = (int)(mh * 0.12f);
    // CROUCH:JUMP 正下方
    crr = (int)(mh * 0.08f);
    cry = jy + jr + crr + 10;
    if (cry + crr > h - 12) cry = h - crr - 12;          // 防出手势条
    // 方块切换:左下角
    bx = (int)(w * 0.10f); br = (int)(mh * 0.06f); by = h - br - 24;
    // MODE:方块切换正上方(不再贴顶,避开状态栏)
    mx0 = (int)(w * 0.02f); mx1 = (int)(w * 0.20f);
    my1 = by - br - 12; my0 = my1 - (int)(h * 0.06f);
    if (my0 < (int)(h * 0.30f)) my0 = (int)(h * 0.30f);
}



// 把 HUD 状态（文字、准星、控制台）绘制到 g_hud.pixels
static void drawHudText(float fps) {
    if (g_hud.pixels.empty()) return;
    memset(g_hud.pixels.data(), 0, g_hud.pixels.size() * sizeof(uint32_t));

    int pcx = toChunk((int)floorf(G::camPos.x));
    int pcz = toChunk((int)floorf(G::camPos.z));
    size_t numChunks;
    { lock_guard<mutex> lk(G::chunksMutex); numChunks = G::chunks.size(); }

    char buf[512];
    snprintf(buf, sizeof(buf),
        "FPS: %.1f\nX: %.1f  Y: %.1f  Z: %.1f\nChunk: (%d,%d)\nLoaded: %zu\nMode: %s",
        fps, G::camPos.x, G::camPos.y, G::camPos.z,
        pcx, pcz, numChunks,
        (G::playerMode == 1) ? "Survival" : "Spectator");
    hudDrawString(20, 20, buf, 0xFFFFFFFF);

    // ★ 诊断行：区块数/GPU网格数/队列长度（排查"只有天空"用）
    {
        size_t nm;
        { lock_guard<mutex> lk(g_meshMutex); nm = g_activeMeshes.size(); }
        char dbg[160];
        snprintf(dbg, sizeof(dbg), "Chunks:%zu Mesh:%zu UpQ:%zu GenQ:%zu MQ:%zu",
                 numChunks, nm, g_uploadJobs.size(), g_genJobs.size(), g_meshJobs.size());
        hudDrawString(20, 140, dbg, 0xFF00FF00);
    }

    // 准星
    {
        int cx = g_hud.width / 2, cy = g_hud.height / 2;
        for (int y = cy - 12; y <= cy - 3; y++)
            if (y >= 0 && y < g_hud.height && cx >= 0 && cx < g_hud.width)
                g_hud.pixels[y * g_hud.width + cx] = 0xFF000000;
        for (int y = cy + 3; y <= cy + 12; y++)
            if (y >= 0 && y < g_hud.height && cx >= 0 && cx < g_hud.width)
                g_hud.pixels[y * g_hud.width + cx] = 0xFF000000;
        for (int x = cx - 12; x <= cx - 3; x++)
            if (x >= 0 && x < g_hud.width && cy >= 0 && cy < g_hud.height)
                g_hud.pixels[cy * g_hud.width + x] = 0xFF000000;
        for (int x = cx + 3; x <= cx + 12; x++)
            if (x >= 0 && x < g_hud.width && cy >= 0 && cy < g_hud.height)
                g_hud.pixels[cy * g_hud.width + x] = 0xFF000000;
    }



    // ★ 新 UI:右上MODE / 右下JUMP / 底部方块圆 / 摇杆
    {
        int w = g_hud.width, h = g_hud.height;
        int jx, jy, jr, crx, cry, crr, bx, by, br, mx0, my0, mx1, my1;
        uiLayout(w, h, jx, jy, jr, crx, cry, crr, bx, by, br, mx0, my0, mx1, my1);

        // MODE:左下,方块切换上方
        for (int yy = my0; yy < my1; yy++)
            for (int xx = mx0; xx < mx1; xx++)
                if (!(xx & 1) || !(yy & 1)) g_hud.pixels[yy * w + xx] = 0x90000000;
        hudDrawString(mx0 + 8, my0 + 4, "MODE", 0xFFFFFFFF);

        // JUMP(加大)
        hudFillCircle(jx, jy, jr, 0x50000000);
        hudRingCircle(jx, jy, jr, G::jumpHeld ? 0xFFFFFF00 : 0xFFFFFFFF);
        hudDrawString(jx - 30, jy - 14, "JUMP", 0xFFFFFFFF);

        // CROUCH(跳跃正下方)
        hudFillCircle(crx, cry, crr, 0x50000000);
        hudRingCircle(crx, cry, crr, G::crouchHeld ? 0xFFFFFF00 : 0xFFFFFFFF);
        hudDrawString(crx - 34, cry - 10, "CROUCH", 0xFFFFFFFF);

        // 方块切换:左下角
        hudFillCircle(bx, by, br, 0x60000000);
        hudRingCircle(bx, by, br, 0xFFFFFFFF);
        { string bn = blockNameCN(G::heldBlock);
        hudDrawString(bx - (int)bn.size() * 7, by - 14, bn.c_str(), 0xFFFFFF00); }

        // 摇杆(静止底座右移,避开 MODE 区)
        if (g_stick.active) { /* 原两行不动 */ }
        else hudRingCircle((int)(w * 0.28f), (int)(h * 0.72f), (int)STICK_R, 0x8090D090);
    }



    // 控制台覆盖层
    if (g_consoleOpen) {
        for (int y = 0; y < g_hud.height / 2; y++)
            for (int x = 0; x < g_hud.width; x++)
                g_hud.pixels[y * g_hud.width + x] = 0xE0000000;
        string prompt = "> " + g_cmdBuf + "_";
        hudDrawString(20, 20, prompt, 0xFFFFFFFF);
        for (size_t i = 0; i < g_cmdLog.size(); i++) {
            char line[256];
            snprintf(line, sizeof(line), "%s", g_cmdLog[i].c_str());
            hudDrawString(20, 60 + (int)i * 30, line, 0xFFB0B0B0);
        }
    }
}


static void uploadHudTexture(VkCommandBuffer cb) {
    VkDeviceSize dataSize = (VkDeviceSize)g_hud.width * g_hud.height * 4;
    if (dataSize > g_hud.stagingSize) return;

    // 把 ARGB 转成 BGRA 再上传（其实我们内部已经是 BGRA 排列了，
    // 因为我们用 nr<<16 | ng<<8 | nb，这与 BGRA 内存布局一致）
    memcpy(g_hud.stagingMapped, g_hud.pixels.data(), dataSize);
    vmaFlushAllocation(g_vk.vma, g_hud.stagingAlloc, 0, dataSize);

    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = g_hud.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &b);

    VkBufferImageCopy region = {};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {(uint32_t)g_hud.width, (uint32_t)g_hud.height, 1};
    vkCmdCopyBufferToImage(cb, g_hud.stagingBuffer, g_hud.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &b);
}

static void recreateHud(int w, int h) {
    if (w <= 0 || h <= 0) return;
    vkDeviceWaitIdle(g_vk.device);

    if (g_hud.stagingBuffer) {
        vmaDestroyBuffer(g_vk.vma, g_hud.stagingBuffer, g_hud.stagingAlloc);
        g_hud.stagingBuffer = VK_NULL_HANDLE;
        g_hud.stagingAlloc = VK_NULL_HANDLE;
        g_hud.stagingMapped = nullptr;
        g_hud.stagingSize = 0;
    }
    if (g_hud.sampler)   { vkDestroySampler(g_vk.device, g_hud.sampler, nullptr);     g_hud.sampler = VK_NULL_HANDLE; }
    if (g_hud.imageView) { vkDestroyImageView(g_vk.device, g_hud.imageView, nullptr); g_hud.imageView = VK_NULL_HANDLE; }
    if (g_hud.image)     { vmaDestroyImage(g_vk.vma, g_hud.image, g_hud.imageAlloc); g_hud.image = VK_NULL_HANDLE; g_hud.imageAlloc = VK_NULL_HANDLE; }

    createHudTexture(w, h);
}


// ---- VK boot 失败弹窗诊断 ----
//static struct android_app* g_app = nullptr;   // android_main 里赋值(改动 A 一并放这)

static void showErrorPopup(const char* msg) {
    static bool _popupStarted = false;
    if (_popupStarted) return;
    _popupStarted = true;

    std::thread([msg]() {
        JavaVM* vm = g_app->activity->vm;
        JNIEnv* env = nullptr;
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || env == nullptr) return;

        // Looper.prepare():后台线程弹窗必须有Looper
        jclass looperCls = env->FindClass("android/os/Looper");
        jmethodID prep = env->GetStaticMethodID(looperCls, "prepare", "()V");
        if (prep == nullptr) return;
        env->CallStaticVoidMethod(looperCls, prep);

        // new AlertDialog.Builder(activity)
        jclass bCls = env->FindClass("android/app/AlertDialog$Builder");
        jmethodID bCtor = env->GetMethodID(bCls, "<init>", "(Landroid/content/Context;)V");
        jobject builder = env->NewObject(bCls, bCtor, g_app->activity->clazz);

        jstring jtitle = env->NewStringUTF("ERROR");
        jmethodID setTitle = env->GetMethodID(bCls, "setTitle",
            "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;");
        env->CallObjectMethod(builder, setTitle, jtitle);

        jstring jmsg = env->NewStringUTF(msg);
        jmethodID setMsg = env->GetMethodID(bCls, "setMessage",
            "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;");
        env->CallObjectMethod(builder, setMsg, jmsg);

        jmethodID createM = env->GetMethodID(bCls, "create", "()Landroid/app/AlertDialog;");
        jobject dlg = env->CallObjectMethod(builder, createM);
        jmethodID showM = env->GetMethodID(env->GetObjectClass(dlg), "show", "()V");
        env->CallVoidMethod(dlg, showM);

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            LOGI("[POPUP] show failed");
            return;
        }
        LOGI("[POPUP] shown: %s", msg);

        // 线程驻留在 Looper 里,弹窗才不会消失;诊断期不需要退出
        jmethodID loop = env->GetStaticMethodID(looperCls, "loop", "()V");
        env->CallStaticVoidMethod(looperCls, loop);
        }).detach();
}

// ============================================================
// Section 14. 渲染帧（带视锥/距离剔除 + surface 丢失保护）
// ============================================================
static void renderFrame(float fps) {
    static bool _rLogged = false;
    if (!_rLogged) { LOGI("[RENDER] first frame entered"); _rLogged = true; }

    if (g_vk.device == VK_NULL_HANDLE) {
        static int _nullFrames = 0;
        ++_nullFrames;
        if (_nullFrames == 120) {          // ~2 秒仍未初始化才弹,防启动期误报
            LOGI("[POPUP] trigger: device null after 120 frames");
            showErrorPopup("ERROR: VK boot");
        }
        return;
    }


    if (g_vk.framebufferResized) {
        g_vk.framebufferResized = false;
        recreateSwapchain();
    }
    if (g_vk.swapchain == VK_NULL_HANDLE || g_vk.swapFramebuffers.empty()) {
        return;   // 无窗口：跳过渲染（游戏逻辑照常）
    }

    vkWaitForFences(g_vk.device, 1, &g_vk.inFlight[g_vk.frameIdx], VK_TRUE, UINT64_MAX);

    uint32_t imageIdx = 0;
    VkResult acq = vkAcquireNextImageKHR(g_vk.device, g_vk.swapchain, UINT64_MAX,
                                         g_vk.imageAvail[g_vk.frameIdx],
                                         VK_NULL_HANDLE, &imageIdx);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR || acq == VK_SUBOPTIMAL_KHR) {
        g_vk.framebufferResized = true;
        return;
    }
    if (acq == VK_ERROR_SURFACE_LOST_KHR) {
        // 窗口已死：销毁 surface 全家桶，等 INIT_WINDOW 用新窗口重建
        destroySwapchainFamily(true);
        return;
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) return;
    if (imageIdx >= g_vk.swapFramebuffers.size() || imageIdx >= g_vk.renderDone.size()) {
        g_vk.framebufferResized = true;
        return;
    }

    XMFLOAT3 eyePos = G::camPos;
    if (G::playerMode == 1) eyePos.y += PLAYER_EYE_HEIGHT;

    XMFLOAT3 dir(sinf(G::yaw)*cosf(G::pitch), sinf(G::pitch), cosf(G::yaw)*cosf(G::pitch));
    XMMATRIX view = XMMatrixLookToLH(eyePos, dir, XMFLOAT3(0, 1, 0));
    XMMATRIX proj = XMMatrixPerspectiveFovLH(
        XMConvertToRadians(75.0f),
        (float)g_vk.swapExtent.width / (float)max(1u, g_vk.swapExtent.height),
        0.1f, 2000.0f);

    XMFLOAT3 camRight(cosf(G::yaw), 0.0f, -sinf(G::yaw));
    XMFLOAT3 camUp = normalize3(cross3(dir, camRight));

    CameraUBO ubo;
    ubo.world = XMMatrixIdentity();
    ubo.view  = view;
    ubo.proj  = proj;
    ubo.lightDir = XMFLOAT3(-0.4f, -0.85f, -0.3f);
    ubo.lightColor = XMFLOAT3(1.0f, 0.97f, 0.90f);
    ubo.ambientColor = XMFLOAT3(0.45f, 0.48f, 0.55f);
    ubo.cameraPos = eyePos;
    ubo.depthFadeStart = -50.0f;
    ubo.depthFadeEnd = 60.0f;
    ubo.cameraRight = camRight;
    ubo.cameraUp = camUp;
    if (g_modSettings.loaded) {
        ubo.skyColor = XMFLOAT3(g_modSettings.skyColor[0], g_modSettings.skyColor[1], g_modSettings.skyColor[2]);
        ubo.fogDensity = g_modSettings.fogEnable ? g_modSettings.fogDensity : 0.0f;
        ubo.fogColor = XMFLOAT3(g_modSettings.fogColor[0], g_modSettings.fogColor[1], g_modSettings.fogColor[2]);
    } else {
        ubo.skyColor = XMFLOAT3(0.55f, 0.65f, 0.80f);
        ubo.fogDensity = 0.0f;
        ubo.fogColor = XMFLOAT3(0.55f, 0.65f, 0.80f);
    }
    ubo.underwater = G::inWater ? 1.0f : 0.0f;
    memcpy(g_vk.camUBOMapped, &ubo, sizeof(ubo));

    // 视锥剔除
    FrustumPlane planes[6];
    extractFrustumPlanes(view * proj, planes);

    VkCommandBuffer cb = g_vk.cmdBuf[g_vk.frameIdx];
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    drawHudText(fps);
    uploadHudTexture(cb);

    VkClearValue clears[2];
    clears[0].color = {{ubo.skyColor.x, ubo.skyColor.y, ubo.skyColor.z, 1.0f}};
    if (G::inWater) clears[0].color = {{0.05f, 0.20f, 0.35f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass = g_vk.renderPass;
    rbi.framebuffer = g_vk.swapFramebuffers[imageIdx];
    rbi.renderArea = {{0, 0}, g_vk.swapExtent};
    rbi.clearValueCount = 2;
    rbi.pClearValues = clears;
    vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0, (float)g_vk.swapExtent.height,
                  (float)g_vk.swapExtent.width, -(float)g_vk.swapExtent.height,
                  0, 1};
    VkRect2D sc{{0, 0}, g_vk.swapExtent};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);

    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            g_vk.pipelineLayout, 0, 1, &g_vk.descSet, 0, nullptr);

    vector<pair<uint64_t, VkActiveChunk*>> opaqueList, transList;
    {
        lock_guard<mutex> lk(g_meshMutex);
        for (auto& kv : g_activeMeshes) {
            int ccx = (int)(kv.first >> 32);
            int ccz = (int)(kv.first & 0xFFFFFFFF);
            // ★ 与 PC 版一致：包围球取整块中心，半径≈半个块对角线，避免误剔除
            XMFLOAT3 center(ccx * 16.0f + 8.0f, (RY0 + RY1) * 0.5f, ccz * 16.0f + 8.0f);
            float halfDiagXZ = 8.0f * 1.4142135f;
            float halfH = (RY1 - RY0) * 0.5f;
            float radius = sqrtf(halfDiagXZ * halfDiagXZ + halfH * halfH);
            float dx = center.x - eyePos.x;
            float dy = center.y - eyePos.y;
            float dz = center.z - eyePos.z;
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dist - radius > RENDER_DISTANCE) continue;
            if (dist < radius + 48.0f) {
                // 相机所在/相邻 chunk:直接画,不做过紧的视锥判定
            }
            else if (!sphereInFrustumD(planes, center, radius)) {
                continue;
            }

            VkActiveChunk* ac = &kv.second;
            if (ac->opaque.valid) opaqueList.push_back({kv.first, ac});
            if (ac->trans.valid)  transList.push_back({kv.first, ac});
        }
    }

    if (g_vk.voxelPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.voxelPipeline);
        for (auto& kv : opaqueList) {
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &kv.second->opaque.buffer, &off);
            vkCmdBindIndexBuffer(cb, kv.second->opaque.buffer,
                                 kv.second->opaque.indexOffset, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, kv.second->opaque.indexCount, 1, 0, 0, 0);
        }
    }

    sort(transList.begin(), transList.end(),
         [&eyePos](const pair<uint64_t, VkActiveChunk*>& a,
                   const pair<uint64_t, VkActiveChunk*>& b) {
        int acx = (int)(a.first >> 32), acz = (int)(a.first & 0xFFFFFFFF);
        int bcx = (int)(b.first >> 32), bcz = (int)(b.first & 0xFFFFFFFF);
        float ax = acx*16.0f + 8.0f, az = acz*16.0f + 8.0f;
        float bx = bcx*16.0f + 8.0f, bz = bcz*16.0f + 8.0f;
        float da = (ax-eyePos.x)*(ax-eyePos.x) + (az-eyePos.z)*(az-eyePos.z);
        float db = (bx-eyePos.x)*(bx-eyePos.x) + (bz-eyePos.z)*(bz-eyePos.z);
        return da > db;
    });

    if (g_vk.voxelPipelineTrans != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.voxelPipelineTrans);
        for (auto& kv : transList) {
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &kv.second->trans.buffer, &off);
            vkCmdBindIndexBuffer(cb, kv.second->trans.buffer,
                                 kv.second->trans.indexOffset, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, kv.second->trans.indexCount, 1, 0, 0, 0);
        }
    }

    {
        ParticleVertex* v = (ParticleVertex*)g_vk.particleVBMapped;
        static const float corners[4][2] = {{-1,-1},{1,-1},{1,1},{-1,1}};
        int count = 0;
        for (int i = 0; i < MAX_PARTICLES && count < MAX_PARTICLES; i++) {
            Particle& p = g_particles[i];
            if (!p.active) continue;
            float lr = p.life / p.maxLife;
            float alpha = (lr > 0.5f) ? 1.0f : (lr * 2.0f);
            for (int c = 0; c < 4; c++) {
                ParticleVertex& pv = v[count * 4 + c];
                pv.center = p.pos;
                pv.size = p.size;
                pv.corner = XMFLOAT2(corners[c][0], corners[c][1]);
                pv.color = p.color;
                pv.alpha = alpha;
            }
            count++;
        }
        vmaFlushAllocation(g_vk.vma, g_vk.particleVBAlloc, 0,
                           (VkDeviceSize)count * 4 * sizeof(ParticleVertex));
        if (count > 0 && g_vk.particlePipeline != VK_NULL_HANDLE) {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.particlePipeline);
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &g_vk.particleVB, &off);
            vkCmdDraw(cb, count * 4, 1, 0, 0);
        }
    }

    if (g_hud.pipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_hud.pipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                g_hud.pipelineLayout, 0, 1, &g_hud.descSet, 0, nullptr);
        vkCmdDraw(cb, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &g_vk.imageAvail[g_vk.frameIdx];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &g_vk.renderDone[imageIdx];

    vkResetFences(g_vk.device, 1, &g_vk.inFlight[g_vk.frameIdx]);
    vkQueueSubmit(g_vk.gfxQueue, 1, &si, g_vk.inFlight[g_vk.frameIdx]);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &g_vk.renderDone[imageIdx];
    pi.swapchainCount = 1;
    pi.pSwapchains = &g_vk.swapchain;
    pi.pImageIndices = &imageIdx;

    VkResult presentResult = vkQueuePresentKHR(g_vk.presentQueue, &pi);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
        g_vk.framebufferResized = true;
    else if (presentResult == VK_ERROR_SURFACE_LOST_KHR)
        destroySwapchainFamily(true);

    g_vk.frameIdx = (g_vk.frameIdx + 1) % MAX_FRAMES_IN_FLIGHT;
    g_vk.frameCounter++;
    processPendingDeletes();

    // ★ 每 120 帧打一次诊断
    static uint64_t s_lastDbg = 0;
    if (g_vk.frameCounter - s_lastDbg >= 120) {
        s_lastDbg = g_vk.frameCounter;
        size_t nm, nc;
        { lock_guard<mutex> lk(g_meshMutex); nm = g_activeMeshes.size(); }
        { lock_guard<mutex> lk(G::chunksMutex); nc = G::chunks.size(); }
        LOGI("[DBG] frame=%llu chunks=%zu meshes=%zu drawnOpaque=%zu drawnTrans=%zu eye=(%.1f,%.1f,%.1f)",
             (unsigned long long)g_vk.frameCounter, nc, nm,
             opaqueList.size(), transList.size(),
             G::camPos.x, G::camPos.y, G::camPos.z);
    }
}


// ============================================================
// Section 15. 触摸输入（左摇杆移动 / 右半屏视角 / 底部按钮区）
// ============================================================




// 物理键盘 → 控制台字符（NDK r21 无 AKeyEvent_getUnicodeChar 可依赖）
static int keycodeToChar(int32_t code) {
    if (code >= AKEYCODE_A && code <= AKEYCODE_Z) return 'a' + (code - AKEYCODE_A);
    if (code >= AKEYCODE_0 && code <= AKEYCODE_9) return '0' + (code - AKEYCODE_0);
    switch (code) {
        case AKEYCODE_MINUS:  return '-';
        case AKEYCODE_PERIOD: return '.';
        case AKEYCODE_COMMA:  return ',';
        case AKEYCODE_SLASH:  return '/';
        case AKEYCODE_SPACE:  return ' ';
        case AKEYCODE_EQUALS: return '=';
        case AKEYCODE_LEFT_BRACKET: return '[';
        case AKEYCODE_RIGHT_BRACKET: return ']';
    }
    return 0;
}

static void handleMotionEvent(AInputEvent* e) {
    int32_t action = AMotionEvent_getAction(e);
    int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
    int32_t idx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    size_t count = AMotionEvent_getPointerCount(e);
    int w = (int)g_vk.swapExtent.width;
    int h = (int)g_vk.swapExtent.height;
    if (w <= 0 || h <= 0) { w = g_width; h = g_height; }

    for (size_t i = 0; i < count; i++) {
        int id = AMotionEvent_getPointerId(e, i);
        float x = AMotionEvent_getX(e, i);
        float y = AMotionEvent_getY(e, i);
        bool isActionPointer = ((int)i == idx);
        bool owned =
            (g_look.active && g_look.pointerId == id) ||
            (g_stick.active && g_stick.pointerId == id) ||
            (g_modeHeld && g_modePid == id) ||
            (g_jumpPid == id);
        if (!owned) continue;

        if (masked == AMOTION_EVENT_ACTION_MOVE) {
            if (g_look.active && g_look.pointerId == id) {
                float ddx = x - g_look.lx, ddy = y - g_look.ly;
                G::yaw += ddx * 0.004f;
                G::pitch -= ddy * 0.004f;
                if (G::pitch > 1.5f) G::pitch = 1.5f;
                if (G::pitch < -1.5f) G::pitch = -1.5f;
                g_look.totalDist += fabsf(ddx) + fabsf(ddy);
                if (g_look.totalDist > 30.0f) g_look.mineArmed = false;   // 滑动取消挖掘
                g_look.lx = x; g_look.ly = y;
            }
            if (g_stick.active && g_stick.pointerId == id) {
                float vx = x - g_stick.ax, vy = y - g_stick.ay;
                float len = sqrtf(vx * vx + vy * vy);
                if (len > STICK_R) { vx *= STICK_R / len; vy *= STICK_R / len; }
                g_stick.dx = vx / STICK_R;
                g_stick.dy = vy / STICK_R;
            }
        }
        else if (masked == AMOTION_EVENT_ACTION_UP ||
            masked == AMOTION_EVENT_ACTION_POINTER_UP ||
            masked == AMOTION_EVENT_ACTION_CANCEL) {
            if (masked == AMOTION_EVENT_ACTION_POINTER_UP && !isActionPointer) continue;

            if (g_look.active && g_look.pointerId == id) {
                int64_t dtMs = nowMs() - g_look.t0;
                if (dtMs < 300 && g_look.totalDist < 25.0f && !g_look.mineFired)
                    G::placeDown = true;              // 轻触 0.3s 内 = 放置
                g_look = {};
            }
            if (g_stick.active && g_stick.pointerId == id) {
                g_stick = {};
            }
            if (g_modeHeld && g_modePid == id) {
                if (!g_modeFired) switchMode(G::playerMode == 1 ? 0 : 1);  // 短按=切模式
                g_modeHeld = false; g_modePid = -1;
            }
            if (g_jumpPid == id) { G::jumpHeld = false;   g_jumpPid = -1; }
            if (g_crouchPid == id) { G::crouchHeld = false; g_crouchPid = -1; }   // ★

        }
    }

    if (masked == AMOTION_EVENT_ACTION_DOWN || masked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        if (g_consoleOpen) return;                    // 控制台开着:全屏吞掉
        int id = AMotionEvent_getPointerId(e, idx);
        float x = AMotionEvent_getX(e, idx);
        float y = AMotionEvent_getY(e, idx);
        float mh2 = (float)((w < h) ? w : h);

        int jx, jy, jr, crx, cry, crr, bx, by, br, mx0, my0, mx1, my1;
        uiLayout(w, h, jx, jy, jr, crx, cry, crr, bx, by, br, mx0, my0, mx1, my1);

        // ① MODE(左下;长按 0.6s=控制台,短按=切模式)
        if (x > mx0 && x < mx1 && y > my0 && y < my1) {
            g_modeHeld = true; g_modePid = id; g_modeT0 = nowMs(); g_modeFired = false;
            return;
        }
        // ② JUMP(加大后的圆)
        { float ddx = x - jx, ddy = y - jy;
        if (ddx * ddx + ddy * ddy <= (float)jr * jr) { G::jumpHeld = true; g_jumpPid = id; return; } }
        // ②b CROUCH(★ 新增)
        { float ddx = x - crx, ddy = y - cry;
        if (ddx * ddx + ddy * ddy <= (float)crr * crr) { G::crouchHeld = true; g_crouchPid = id; return; } }
        // ③ 方块切换(左下角)
        { float ddx = x - bx, ddy = y - by;
        if (ddx * ddx + ddy * ddy <= (float)br * br) {
            G::heldBlockIndex = (G::heldBlockIndex + 1) % g_selectableCount;
            G::heldBlock = g_selectableBlocks[G::heldBlockIndex];
            return;
        } }
        // ④ 摇杆 ⑤ 视角:原样不动

        // ④ 左下摇杆区(按下点=浮动圆心)
        if (x < w * 0.40f && y > h * 0.40f) {
            g_stick.active = true; g_stick.pointerId = id;
            g_stick.ax = x; g_stick.ay = y;
            g_stick.dx = 0; g_stick.dy = 0;
            return;
        }
        // ⑤ 其余全屏:视角 + 轻触/长按计时
        g_look.active = true; g_look.pointerId = id;
        g_look.sx = g_look.lx = x;
        g_look.sy = g_look.ly = y;
        g_look.t0 = nowMs();
        g_look.totalDist = 0;
        g_look.mineFired = false;
        g_look.mineArmed = true;
    }
}


static void handleKeyEvent(AInputEvent* e) {
    int32_t code = AKeyEvent_getKeyCode(e);
    int32_t act  = AKeyEvent_getAction(e);
    bool down = (act == AKEY_EVENT_ACTION_DOWN);

    // 控制台打开时：按键输入命令
    if (g_consoleOpen) {
        if (!down) return;
        if (code == AKEYCODE_BACK || code == AKEYCODE_GRAVE || code == AKEYCODE_ESCAPE) {
            g_consoleOpen = false;
            return;
        }
        if (code == AKEYCODE_ENTER || code == AKEYCODE_NUMPAD_ENTER) {
            execCommand(g_cmdBuf);
            g_cmdBuf.clear();
            return;
        }
        if (code == AKEYCODE_DEL) {
            if (!g_cmdBuf.empty()) g_cmdBuf.pop_back();
            return;
        }
        int uc = keycodeToChar(code);
        if (uc >= 32 && uc < 127) {
            g_cmdBuf.push_back((char)uc);
        }
        return;
    }

    switch (code) {
        case AKEYCODE_GRAVE:
            if (down) { g_consoleOpen = true; g_cmdBuf.clear(); }
            break;
        case AKEYCODE_W:     G::keys['W'] = down; break;
        case AKEYCODE_A:     G::keys['A'] = down; break;
        case AKEYCODE_S:     G::keys['S'] = down; break;
        case AKEYCODE_D:     G::keys['D'] = down; break;
        case AKEYCODE_SPACE: G::keys[VK_SPACE] = down; break;
        case AKEYCODE_SHIFT_LEFT:
        case AKEYCODE_SHIFT_RIGHT: G::keys[VK_SHIFT] = down; break;
        case AKEYCODE_Q:     G::keys['Q'] = down; break;
        case AKEYCODE_E:     G::keys['E'] = down; break;
        case AKEYCODE_M:     if (down) switchMode(1); break;
        case AKEYCODE_N:     if (down) switchMode(0); break;
    }
}

static int32_t handleInput(struct android_app* /*app*/, AInputEvent* e) {
    int32_t type = AInputEvent_getType(e);
    if (type == AINPUT_EVENT_TYPE_MOTION) { handleMotionEvent(e); return 1; }
    if (type == AINPUT_EVENT_TYPE_KEY)    { handleKeyEvent(e);    return 1; }
    return 0;
}


// ============================================================
// Mod 配置 / 存档路径 / 窗口生命周期 / android_main
// ============================================================
static string g_storageBase;      // /storage/emulated/0/Android/data/com.voxel.android2.0

static void LoadModConfigFromString(const string& text) {
    try {
        json j = json::parse(text);
        g_modSettings.loaded = true;
        if (j.contains("Sky")) {
            auto& s = j["Sky"];
            if (s.contains("ColorR")) g_modSettings.skyColor[0] = s["ColorR"].get<float>();
            if (s.contains("ColorG")) g_modSettings.skyColor[1] = s["ColorG"].get<float>();
            if (s.contains("ColorB")) g_modSettings.skyColor[2] = s["ColorB"].get<float>();
        }
        if (j.contains("Fog")) {
            auto& f = j["Fog"];
            if (f.contains("Enable"))    g_modSettings.fogEnable = f["Enable"].get<bool>();
            if (f.contains("Density"))   g_modSettings.fogDensity = f["Density"].get<float>();
            if (f.contains("ColorR"))    g_modSettings.fogColor[0] = f["ColorR"].get<float>();
            if (f.contains("ColorG"))    g_modSettings.fogColor[1] = f["ColorG"].get<float>();
            if (f.contains("ColorB"))    g_modSettings.fogColor[2] = f["ColorB"].get<float>();
        }
        if (j.contains("Terrain")) {
            auto& t = j["Terrain"];
            if (t.contains("NoiseAmplitude")) g_modSettings.noiseAmplitude = t["NoiseAmplitude"].get<float>();
            if (t.contains("FlatMode"))       g_modSettings.flatMode = t["FlatMode"].get<bool>();
            if (t.contains("NoHeightLimit"))  g_modSettings.noHeightLimit = t["NoHeightLimit"].get<bool>();
            if (t.contains("GridMode"))       g_modSettings.gridMode = t["GridMode"].get<bool>();
        }
        LOGI("[MOD] mod.json loaded");
    } catch (...) {
        g_modSettings.loaded = false;
        LOGE("[MOD] mod.json parse failed");
    }
}

// 存档根目录：外部存储 /Android/data/com.voxel.android2.0/（去掉 externalDataPath 尾部的 /files）
static void loadModFromStorage() {
    string p = g_storageBase + "/mod.json";
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) { LOGI("[MOD] no mod.json, using defaults"); return; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (1 << 20)) { fclose(f); return; }
    string s((size_t)n, '\0');
    fread(&s[0], 1, (size_t)n, f);
    fclose(f);
    LoadModConfigFromString(s);
}

static uint64_t makeRandomSeed() {
    std::random_device rd;
    uint64_t a = ((uint64_t)rd() << 32) | (uint64_t)rd();
    uint64_t b = (uint64_t)chrono::high_resolution_clock::now()
                     .time_since_epoch().count();
    uint64_t s = a ^ b;
    if (s == 0) s = 0xFEEDFACE12345678ULL;
    return s;
}

// 初始化世界：优先加载存储目录里最新的 .map，否则新建
static bool initWorldFromStorage() {
    const char* ext = g_app->activity->externalDataPath;
    if (!ext || !ext[0]) ext = g_app->activity->internalDataPath;
    string base = ext ? ext : "/data/local/tmp";
    // /storage/emulated/0/Android/data/<pkg>/files → 去掉 /files
    if (base.size() >= 6 && base.substr(base.size() - 6) == "/files")
        base = base.substr(0, base.size() - 6);
    g_storageBase = base;
    mkdir(base.c_str(), 0755);
    LOGI("[STORAGE] base = %s", base.c_str());

    // 找最新的 .map
    string newest;
    time_t newestT = 0;
    DIR* d = opendir(base.c_str());
    if (d) {
        struct dirent* de;
        while ((de = readdir(d)) != nullptr) {
            string name = de->d_name;
            if (name.size() < 5 || name.substr(name.size() - 4) != ".map") continue;
            struct stat st;
            string full = base + "/" + name;
            if (stat(full.c_str(), &st) == 0 && st.st_mtime >= newestT) {
                newestT = st.st_mtime;
                newest = full;
            }
        }
        closedir(d);
    }

    if (!newest.empty()) {
        if (Save::g_store.open(newest, false)) {
            LOGI("[WORLD] loaded %s", newest.c_str());
            McNoiseInit(G::seed);
            return true;
        }
        LOGE("[WORLD] open %s failed, fallback to new world", newest.c_str());
    }
    // 新建世界
    G::seed = makeRandomSeed();
    reseedAllNoise(G::seed);
    rollTuning(G::seed);
    generateTubes(G::seed);
    G::camPos = XMFLOAT3(0.5f, 200.0f, 0.5f);
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char fname[64];
    strftime(fname, sizeof(fname), "%Y%m%d%H%M.map", &tmv);
    string path = base + "/" + fname;
    if (!Save::g_store.open(path, true)) {
        LOGE("[WORLD] create %s failed", path.c_str());
        return false;
    }
    LOGI("[WORLD] new world seed=%llX file=%s",
         (unsigned long long)G::seed, path.c_str());
    McNoiseInit(G::seed);
    return true;
}



static void processTouchActions() {
    if (!G::placeDown && !G::breakDown) return;
    XMFLOAT3 rayOrigin = G::camPos;
    if (G::playerMode == 1) rayOrigin.y += PLAYER_EYE_HEIGHT;
    XMFLOAT3 dir(sinf(G::yaw)*cosf(G::pitch), sinf(G::pitch), cosf(G::yaw)*cosf(G::pitch));
    VoxelHit hit = raycastVoxel(rayOrigin, dir, 8.0f);

    if (hit.hit) {
        if (G::placeDown) {
            if (!blockIntersectsPlayer((float)hit.px, (float)hit.py, (float)hit.pz)) {
                setBlock(hit.px, hit.py, hit.pz, G::heldBlock);
                markChunksDirtyAround(hit.px, hit.py, hit.pz);
            }
        } else if (G::breakDown) {
            Block broken = getBlock(hit.x, hit.y, hit.z);
            setBlock(hit.x, hit.y, hit.z, Block::AIR);
            markChunksDirtyAround(hit.x, hit.y, hit.z);
            spawnBlockBreakParticles(hit.x, hit.y, hit.z, broken);
        }
    }
    G::placeDown = false;
    G::breakDown = false;
}

// ============================================================
// Section 16. android_main 入口（窗口重建修复）
// ============================================================
static void handleAppCmd(android_app* app, int32_t cmd) {
    LOGI("[CMD] cmd=%d", cmd);          // ← 加这一行
    switch (cmd) {

        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                if (g_vk.instance == VK_NULL_HANDLE) {
                    if (!initVulkan(app->window)) {
                        showErrorPopup("ERROR: VK boot (initVulkan failed)");

                        LOGE("Vulkan init failed");
                        ANativeActivity_finish(app->activity);
                    }
                } else {
                    // ★ 窗口重建：旧 surface 已随旧窗口失效，销毁后按新窗口重建。
                    //   device/pipeline/VMA/网格全程只建一次，不再触发 VMA 池断言。
                    destroySwapchainFamily(true);
                    if (!createSurfaceAndSwapchain(app->window))
                        g_vk.framebufferResized = true;   // 稍后重试
                }
            }
            break;
        case APP_CMD_TERM_WINDOW:
            destroySwapchainFamily(true);
            for (int i = 0; i < 256; i++) G::keys[i] = false;
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            if (g_vk.instance != VK_NULL_HANDLE) g_vk.framebufferResized = true;
            break;
        case APP_CMD_LOST_FOCUS:
            for (int i = 0; i < 256; i++) G::keys[i] = false;
            break;
        case APP_CMD_GAINED_FOCUS:
        case APP_CMD_PAUSE:
        case APP_CMD_RESUME:
        case APP_CMD_SAVE_STATE:
        default:
            break;
    }
}

void android_main(struct android_app* app) {
    g_app = app;
    app->onAppCmd = handleAppCmd;
    app->onInputEvent = handleInput;

    initBlockInfo();
    initTerrainSplines();

    loadModFromStorage();          // 必须在 rollTuning 之前
    if (!initWorldFromStorage()) {
        ANativeActivity_finish(app->activity);
        return;
    }

    startWorkers();

    auto last = chrono::steady_clock::now();
    int frames = 0;
    double fpsTimer = 0;
    float fps = 0.0f;
    auto lastSaveTime = chrono::steady_clock::now();

    while (!app->destroyRequested) {
        int events;
        android_poll_source* source;
        int r;
        while ((r = ALooper_pollOnce(0, nullptr, &events, (void**)&source)) >= 0) {
            if (source != nullptr) source->process(app, source);
        }
        if (r == ALOOPER_POLL_ERROR) { LOGI("[MAIN] exit: poll error"); break; }
        if (app->destroyRequested != 0) { LOGI("[MAIN] exit: destroyRequested"); break; }

        if (app->destroyRequested) break;
        if (!app->window || g_vk.device == VK_NULL_HANDLE || g_vk.swapchain == VK_NULL_HANDLE
            || g_vk.swapFramebuffers.empty()) {
            this_thread::sleep_for(chrono::milliseconds(16));
            continue;
        }

        // ↓ 这里开始,for 后面的原有代码【原样保留、一层不动】



        auto now = chrono::steady_clock::now();
        float dt = chrono::duration<float>(now - last).count();
        if (dt > 0.1f) dt = 0.1f;
        last = now;
        fpsTimer += dt; frames++;
        if (fpsTimer >= 1.0) {
            fps = (float)(frames / fpsTimer);
            frames = 0; fpsTimer = 0;
            LOGI("[FPS] %.1f chunks=%zu mode=%s", fps, G::chunks.size(),
                 (G::playerMode == 1) ? "Survival" : "Spectator");
        }

        if (!g_consoleOpen) updateCamera(dt);

        updateParticles(dt);
        streamingUpdate();
        if (g_look.active && !g_look.mineFired && g_look.mineArmed
            && !g_consoleOpen && nowMs() - g_look.t0 >= 300) {
            G::breakDown = true;                    // 按住≥0.3s 未滑动 = 挖掘一次
            g_look.mineFired = true;
        }
        if (g_modeHeld && !g_modeFired && nowMs() - g_modeT0 >= 600) {
            g_consoleOpen = true; g_cmdBuf.clear(); // MODE 长按 0.6s = 控制台
            g_modeFired = true;
        }
        processTouchActions();


        if (chrono::duration<float>(now - lastSaveTime).count() >= 2.0f) {
            lastSaveTime = now;
            flushAllDirty();
        }

        renderFrame(fps);
    }

    LOGI("[SAVE] Shutdown...");
    flushAllDirty();
    drainSaves();
    stopWorkers();
    Save::g_store.flush();
    Save::g_store.close();
    cleanupVulkan();
    LOGI("[SAVE] Done.");
}


