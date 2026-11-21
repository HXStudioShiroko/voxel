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

static const int LOAD_RADIUS   = 10;
static const int UNLOAD_RADIUS = 14;
static const int LOAD_BUDGET_PER_FRAME = 2;
static const int STARTUP_FAST_LOAD_UNTIL = 100;
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
    wstring  path;
    unordered_map<uint64_t, uint64_t> chunkOffsets;
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
        _fseeki64(fp, 0, SEEK_END);
        uint64_t off = (uint64_t)_ftelli64(fp);
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
        _fseeki64(fp, (long long)sizeof(FileHeader), SEEK_SET);
        ChunkRecord rec;
        while (fread(&rec, sizeof(rec), 1, fp) == 1) {
            if (rec.tag != RECORD_TAG) break;
            int64_t dataPos = _ftelli64(fp);
            chunkOffsets[chunkKey(rec.cx, rec.cz)] = (uint64_t)dataPos;
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
struct PerFrameCB {
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
    
    // ★ 核心：新增大气与雾效参数（严格 16 字节对齐）
    XMFLOAT3 skyZenithColor; float fogDensity;   // 天顶色 + 雾浓度
    XMFLOAT3 skyHorizonColor;
    float godRayIntensity;
    float gTime;            // ★ 新增:秒
    float pad9, pad10, pad11;

};

// ★ 粒子着色器
static const char* g_particleShaderSource = R"(
cbuffer PerFrameCB : register(b0) {
    row_major matrix gWorld;
    row_major matrix gView;
    row_major matrix gProj;
    float3 gLightDir;    float gPad1;
    float3 gLightColor;  float gPad2;
    float3 gAmbientColor; float gPad3;
    float3 gCameraPos;   float gPad4;
    float  gDepthFadeStart; float gPad5;
    float  gDepthFadeEnd;   float gPad6;
    float3 gCameraRight; float gPad7;
    float3 gCameraUp;    float gPad8;
    // ★ 补全后面新增的变量，防止读取越界
    float3 gSkyZenith;   float gFogDensity;
    float3 gSkyHorizon;  float gGodRayIntensity;
};

struct VSIn {
    float3 center : POSITION;
    float  size   : SIZE;
    float2 corner : CORNER;
    float3 color  : COLOR;
    float  alpha  : ALPHA;
};

struct VSOut {
    float4 pos   : SV_POSITION;
    float3 color : COLOR;
    float  alpha : ALPHA;
};

VSOut VSParticle(VSIn input) {
    VSOut o;
    float3 worldPos = input.center
                    + (gCameraRight * input.corner.x
                     + gCameraUp    * input.corner.y) * input.size;
    o.pos = mul(mul(float4(worldPos, 1.0), gView), gProj);
    o.color = input.color;
    o.alpha = input.alpha;
    return o;
}

float4 PSParticle(VSOut input) : SV_TARGET {
    return float4(input.color, input.alpha);
}
)";

static const char* g_fullscreenShaderSource = R"(
Texture2D gScreenTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut VSFullscreen(uint id : SV_VertexID) {
    VSOut o;
    // 利用顶点 ID 生成全屏三角形（不用传 VB）
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float3 ACESFilm(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

float4 PSFullscreen(VSOut input) : SV_TARGET {
    float3 c = gScreenTexture.Sample(gSampler, input.uv).rgb;
    // 光照输出已经是 LDR，不需要 Reinhard（它会把中间调往下压，画面发灰）
    c = saturate(c);

    float contrast = 1.22f;      // 还灰就加到 1.30
    c = (c - 0.5f) * contrast + 0.5f;

    float saturation = 1.32f;    // 还灰就加到 1.40
    float lum = dot(c, float3(0.299f, 0.587f, 0.114f));
    c = lerp(float3(lum, lum, lum), c, saturation);

    c = saturate(c);
    c = pow(c, 1.0f / 2.2f);
    return float4(c, 1.0f);
}
)";

static const char* g_shaderSource = R"(
cbuffer PerFrameCB : register(b0) {
    row_major matrix gWorld;
    row_major matrix gView;
    row_major matrix gProj;
    float3 gLightDir;    float gPad1;
    float3 gLightColor;  float gPad2;
    float3 gAmbientColor; float gPad3;
    float3 gCameraPos;   float gPad4;
    float  gDepthFadeStart; float gPad5;
    float  gDepthFadeEnd;   float gPad6;
    float3 gCameraRight; float gPad7;
    float3 gCameraUp;    float gPad8;
    
    float3 gSkyZenith;   float gFogDensity;
    float3 gSkyHorizon;
    float gGodRayIntensity;
    float gTime;            // ★ 新增
    float3 gPad9;

};

Texture2DArray gTexArray : register(t0);
SamplerState   gSampler  : register(s0);

struct VSIn {
    float3 pos      : POSITION;
    float3 normal   : NORMAL;
    float3 color    : COLOR;
    float2 uv       : TEXCOORD0;
    float  texLayer : TEXLAYER;
    float  shade    : SHADE;
    float  light    : LIGHT;    // ★ 必须要有这个，和 C++ 端 LIGHT 对应
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
};

VSOut VSMain(VSIn input) {
    VSOut o;
    float4 wp = mul(float4(input.pos, 1.0), gWorld);
    o.worldPos = wp.xyz;
    o.pos = mul(mul(wp, gView), gProj);
    o.normal = input.normal;
    o.color = input.color;
    o.uv = input.uv;
    o.texLayer = input.texLayer;
    o.shade = input.shade;
    o.light = input.light;       // ★ 必须要有这一行！把光照传给下一步
    return o;
}

float depthFactor(float y) {
    float t = saturate((y - gDepthFadeStart) / (gDepthFadeEnd - gDepthFadeStart));
    t = t * t;
    return 0.30 + 0.70 * t;
}

float3 applyLight(VSOut input) {
    float3 N = normalize(input.normal);
    float3 L = float3(0.707f, 0.707f, 0.0f);
    float3 V = normalize(gCameraPos - input.worldPos);

    float ndotl = dot(N, L);
    float diffuseFactor = 0.5f + 0.5f * ndotl;   // ← 半兰伯特:背光 0.25~0.5,不归零
    // 原来的 smoothstep(0.0f, 1.0f, ndotl * 0.5f + 0.5f) 删掉

    float specMask = step(0.01f, ndotl);
    float3 H = normalize(L + V);
    float specFactor = pow(saturate(dot(N, H)), 32.0f) * specMask;
    float3 specularColor = gLightColor * specFactor * 0.15f;

    // ★ 体素光底光：0 → 0.40（幽闭恐惧症友好），15 → 1.0
    //   想更亮改 0.50，想更暗改 0.35
    const float CAVE_FLOOR = 0.40f;
    float lv = saturate(input.light);
    float lightScale = CAVE_FLOOR + (1.0f - CAVE_FLOOR) * pow(lv, 1.25f);

    float skyAmbientFactor = saturate(N.y * 0.5f + 0.5f);
    float3 ambient = gAmbientColor * 1.15f;             // ← 直接用常数
    // 原来 skyAmbientFactor 那两行删掉

    float3 diffuse = gLightColor * diffuseFactor * 0.55f; 

    float3 totalLight = saturate(ambient + diffuse);   // ★ 不再乘 heightFade

    float3 baseColor = input.texLayer >= 0.0 ?
        gTexArray.Sample(gSampler, float3(input.uv, input.texLayer)).rgb * input.shade
        : input.color * input.shade;

    float3 lit = baseColor * totalLight * lightScale + specularColor * lv;

    // 雾：只留远景薄雾，强度打 6 折，去灰蒙
    float dist = length(input.worldPos - gCameraPos);
    float fog = 1.0f - exp(-(dist * gFogDensity) * (dist * gFogDensity));
    float3 viewDir = normalize(input.worldPos - gCameraPos);
    float heightFactor = saturate(viewDir.y * 1.5f + 0.5f);
    float3 fogColor = lerp(gSkyHorizon, gSkyZenith, heightFactor);
    float fogFade = max(saturate((input.worldPos.y + 30.0f) / 50.0f), 0.30f);
    fogColor *= fogFade;
    float fogMix = max(saturate(fog) * 0.6f, saturate(dist * gFogDensity * 2.5f));
    lit = lerp(lit, fogColor, fogMix);

    return lit;
}
float4 PSMain(VSOut input) : SV_TARGET {
    return float4(applyLight(input), 1.0);
}

float4 PSTrans(VSOut input) : SV_TARGET {
    float3 c = applyLight(input);

    // ===== 水面特效:仅露天水的顶面(天光接近满级才生效) =====
    if (input.texLayer < 0.0f && input.normal.y > 0.5f && input.light > 0.90f) {
        float t = gTime;
        float2 p = input.worldPos.xz;
        float wx = sin(p.x * 2.3f + t * 1.6f) * 0.5f
                 + sin(p.x * 5.7f - t * 2.2f + p.y * 1.3f) * 0.25f;
        float wz = sin(p.y * 2.7f - t * 1.4f) * 0.5f
                 + sin(p.y * 4.9f + t * 2.6f + p.x * 1.1f) * 0.25f;
        float3 N = normalize(float3(wx * 0.15f, 1.0f, wz * 0.15f));
        float3 V = normalize(gCameraPos - input.worldPos);
        float3 L = float3(0.707f, 0.707f, 0.0f);

        float fresnel = pow(1.0f - saturate(dot(N, V)), 3.0f);
        // ★ 反射色用固定水色调色板,不再读 gSkyHorizon/gSkyZenith
        float3 skyRefl = lerp(float3(0.55f, 0.68f, 0.82f),   // 近掠射:亮灰蓝
                              float3(0.30f, 0.52f, 0.80f),   // 俯视:深湖蓝
                              saturate((-V).y * 1.5f + 0.5f));
        c = lerp(c, skyRefl, saturate(0.20f + 0.50f * fresnel));

        float3 R = reflect(-V, N);
        float sun = pow(saturate(dot(R, L)), 120.0f);
        c += gLightColor * sun * 0.9f;
        c += gLightColor * saturate(wx * wz) * 0.06f;
    }
    return float4(c, 0.7);
}


)";

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
    dtd.Width = g_width; dtd.Height = g_height;
    dtd.MipLevels = 1; dtd.ArraySize = 1;
    dtd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dtd.SampleDesc.Count = 1;
    dtd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
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
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    g_device->CreateTexture2D(&depthDesc, nullptr, g_offscreenDepthTex.GetAddressOf());
    g_device->CreateDepthStencilView(g_offscreenDepthTex.Get(), nullptr, g_offscreenDSV.GetAddressOf());

    // 编译全屏着色器
    ID3DBlob* fsVSB = nullptr; ID3DBlob* fsPSB = nullptr;
    D3DCompile(g_fullscreenShaderSource, strlen(g_fullscreenShaderSource), nullptr, nullptr, nullptr, "VSFullscreen", "vs_5_0", 0, 0, &fsVSB, nullptr);
    D3DCompile(g_fullscreenShaderSource, strlen(g_fullscreenShaderSource), nullptr, nullptr, nullptr, "PSFullscreen", "ps_5_0", 0, 0, &fsPSB, nullptr);
    g_device->CreateVertexShader(fsVSB->GetBufferPointer(), fsVSB->GetBufferSize(), nullptr, g_fsVS.GetAddressOf());
    g_device->CreatePixelShader(fsPSB->GetBufferPointer(), fsPSB->GetBufferSize(), nullptr, g_fsPS.GetAddressOf());
    fsVSB->Release(); fsPSB->Release();

    // 创建采样器
    D3D11_SAMPLER_DESC samDesc = {};
    samDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    g_device->CreateSamplerState(&samDesc, g_fsSampler.GetAddressOf());
    // ===================================
        return true;
    };

    if (!compile("VSMain", "vs_5_0", &vsBlob)) return false;
    if (!compile("PSMain", "ps_5_0", &psBlob)) return false;
    if (!compile("PSTrans", "ps_5_0", &psTransBlob)) return false;

    g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                  nullptr, g_vs.GetAddressOf());
    g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                 nullptr, g_ps.GetAddressOf());
    g_device->CreatePixelShader(psTransBlob->GetBufferPointer(), psTransBlob->GetBufferSize(),
                                 nullptr, g_psTrans.GetAddressOf());

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXLAYER", 0, DXGI_FORMAT_R32_FLOAT,       0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "SHADE",    0, DXGI_FORMAT_R32_FLOAT,       0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "LIGHT",    0, DXGI_FORMAT_R32_FLOAT,       0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0 }, // ★ 新增
    };
    g_device->CreateInputLayout(layout, 7, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), g_inputLayout.GetAddressOf());

    vsBlob->Release(); psBlob->Release(); psTransBlob->Release();

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
    if (!jobs.empty() || moved) {
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
            L"Mode: %s",
            fps, G::camPos.x, G::camPos.y, G::camPos.z,
            pcx, pcz, chunkSerial(pcx, pcz),
            numChunks,
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

static void renderFrame(float fps) {
    (void)fps;

    // ---- ① 探测是否在水中(最先,后面都要用)----
    XMFLOAT3 eyePosPre = G::camPos;
    if (G::playerMode == 1) eyePosPre.y += PLAYER_EYE_HEIGHT;
    bool underwater = (getBlock((int)floorf(eyePosPre.x),
        (int)floorf(eyePosPre.y),
        (int)floorf(eyePosPre.z)) == Block::WATER);

    // ---- ② 清屏色 ----
    float clearColor[4] = { g_modSettings.skyColor[0], g_modSettings.skyColor[1],
                            g_modSettings.skyColor[2], 1.0f };
    if (underwater) { clearColor[0] = 0.03f; clearColor[1] = 0.12f; clearColor[2] = 0.28f; }






    // 2. 绑定离屏 RT 并清屏
    ID3D11RenderTargetView* offRtvs[] = { g_offscreenRTV.Get() };
    g_context->OMSetRenderTargets(1, offRtvs, g_offscreenDSV.Get());
    g_context->ClearRenderTargetView(g_offscreenRTV.Get(), clearColor);
    g_context->ClearDepthStencilView(g_offscreenDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

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
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(75.0f),
                                             (float)g_width / g_height, 0.1f, 2000.0f);

    XMMATRIX vpMat = view * proj;
    FrustumPlane planes[6];
    extractFrustumPlanes(vpMat, planes);

    PerFrameCB cb;               // ← 原有
    cb.world = XMMatrixIdentity();
    cb.view = view;
    cb.proj = proj;

    // ---- ④ 时间 uniform(必须在这,cb 已存在)----
    static const auto t0 = chrono::steady_clock::now();
    cb.gTime = chrono::duration<float>(chrono::steady_clock::now() - t0).count();

    // 光线从东方(+X) 25度斜向下射来。
    // 数学换算：X = cos(25°) ≈ 0.906, Y = sin(25°) ≈ 0.423
    // 因为 gLightDir 代表光线传播方向，所以是负值
    // 45度角的光源方向，0.707 是 cos/sin 45度 的常数
    cb.lightDir = XMFLOAT3(-0.707f, -0.707f, 0.0f);
    // 阳光给个正常的 0.9
    cb.lightColor   = XMFLOAT3(1.05f, 1.00f, 0.95f);  // 阳光稍强，拉开明暗（原 0.9）
    cb.ambientColor = XMFLOAT3(0.28f, 0.30f, 0.36f);  // 环境光降一点，去灰（原 0.35）
    cb.cameraPos = eyePos;
    cb.depthFadeStart = -50.0f;
    cb.depthFadeEnd   =  60.0f;

    XMFLOAT3 camRight(cosf(G::yaw), 0.0f, -sinf(G::yaw));
    XMVECTOR upV = XMVector3Cross(dirV, XMLoadFloat3(&camRight));
    XMFLOAT3 camUp;
    XMStoreFloat3(&camUp, XMVector3Normalize(upV));

    cb.cameraRight = camRight;
    cb.cameraUp = camUp;

    if (underwater) {
        cb.skyZenithColor = XMFLOAT3(0.05f, 0.18f, 0.35f);
        cb.skyHorizonColor = XMFLOAT3(0.03f, 0.12f, 0.28f);   // 深海蓝
        cb.fogDensity = 0.06f;                                 // 浓雾:15 格外基本看不见
        cb.godRayIntensity = 0.0f;
    }
    else if (g_modSettings.loaded && g_modSettings.fogEnable) {
        cb.skyZenithColor = XMFLOAT3(0.2f, 0.4f, 0.8f);
        cb.skyHorizonColor = XMFLOAT3(0.5f, 0.7f, 0.9f); // 更清澈的淡蓝色
        cb.fogDensity = 0.00045f;
        cb.godRayIntensity = 0.0f;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    g_context->Map(g_cbuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &cb, sizeof(cb));
    g_context->Unmap(g_cbuffer.Get(), 0);

    UINT stride = sizeof(Vertex), offset = 0;
    g_context->IASetInputLayout(g_inputLayout.Get());
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* cbufs[] = { g_cbuffer.Get() };
    g_context->VSSetConstantBuffers(0, 1, cbufs);
    g_context->PSSetConstantBuffers(0, 1, cbufs);

    ID3D11ShaderResourceView* srvs[] = { g_texArraySRV.Get() };
    g_context->PSSetShaderResources(0, 1, srvs);
    ID3D11SamplerState* samps[] = { g_sampler.Get() };
    g_context->PSSetSamplers(0, 1, samps);

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
            if (dist - radius > RENDER_DISTANCE) continue;

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

    // ================= 后处理阶段 =================

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
    
    ID3D11ShaderResourceView* postSrvs[] = { g_offscreenSRV.Get() };
    g_context->PSSetShaderResources(0, 1, postSrvs);
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
                    if (w == 'M') switchMode(1);
                    else if (w == 'N') switchMode(0);
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