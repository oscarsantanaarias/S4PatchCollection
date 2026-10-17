#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <detours.h>
#include "../s4_base.h"

// ============================================================================
// S4 League Season 8 - Framerate-Independent Collision Resolver Hook
// Fixes player freezing at >60 FPS, wall catching, ledge tunneling, and jitters.
// ============================================================================

static inline float* fF(char* base, int offset) { return reinterpret_cast<float*>(base + offset); }
static inline double* fD(char* base, int offset) { return reinterpret_cast<double*>(base + offset); }
static inline int* fI(char* base, int offset) { return reinterpret_cast<int*>(base + offset); }
static inline uint32_t* fU(char* base, int offset) { return reinterpret_cast<uint32_t*>(base + offset); }
static inline uint8_t* fB(char* base, int offset) { return reinterpret_cast<uint8_t*>(base + offset); }

static inline float vec3_len_sq(const float* v) {
    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

static inline float vec3_len(const float* v) {
    return std::sqrt(vec3_len_sq(v));
}

static inline void vec3_norm(float* v) {
    float len = vec3_len(v);
    if (len > 0.0f) {
        float inv = 1.0f / len;
        v[0] *= inv;
        v[1] *= inv;
        v[2] *= inv;
    }
}

static inline float vec3_dot(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline float vec3_dist(const float* a, const float* b) {
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

struct alignas(4) SAABBLayout {
    uint32_t vt;
    float min_x, min_y, min_z;
    float max_x, max_y, max_z;
};

static inline void saabb_union(SAABBLayout* a, const SAABBLayout* b) {
    if (a->min_x > b->min_x) a->min_x = b->min_x;
    if (a->min_y > b->min_y) a->min_y = b->min_y;
    if (a->min_z > b->min_z) a->min_z = b->min_z;
    if (b->max_x > a->max_x) a->max_x = b->max_x;
    if (b->max_y > a->max_y) a->max_y = b->max_y;
    if (b->max_z > a->max_z) a->max_z = b->max_z;
}

static float ReadConvar(unsigned int index) {
    typedef void* (__cdecl* FnGetRoot)();
    typedef void* (__thiscall* FnLookup)(void* this_ptr, void* out_buf, unsigned int idx);
    typedef float (__thiscall* FnReadFloat)(void* this_ptr);

    static auto fnGetRoot = reinterpret_cast<FnGetRoot>(S4(0x004AE0A0));
    static auto fnLookup = reinterpret_cast<FnLookup>(S4(0x01053900));
    static auto fnReadFloat = reinterpret_cast<FnReadFloat>(S4(0x004AD540));

    void* root = fnGetRoot();
    if (!root) return 0.0f;
    uint32_t buf[3] = { 0, 0, 0 };
    fnLookup(root, buf, index);
    return fnReadFloat(buf);
}

static inline uint32_t GetCollisionMask() {
    return *reinterpret_cast<uint32_t*>(S4(0x015F4218));
}

static inline float GetCollisionEpsilon() {
    return *reinterpret_cast<float*>(S4(0x015F4210));
}

typedef float* (__fastcall* FnCollisionResolver)(
    char* self, int edx,
    float* a2_out,
    float a3_in, float a4_in, float a5_in,
    float a6_in, float a7_in, float a8_in,
    uint32_t* a9_depth);

static FnCollisionResolver OriginalCollisionResolver = nullptr;

static float* __fastcall HookCollisionResolverInternal(
    char* self, int edx,
    float* a2_out,
    float a3_in, float a4_in, float a5_in,
    float a6_in, float a7_in, float a8_in,
    uint32_t* a9_depth)
{
    char* o = self;

    float pos_buf[3] = { a3_in, a4_in, a5_in };
    float vel_buf[3] = { a6_in, a7_in, a8_in };
    float& a3 = pos_buf[0]; float& a4 = pos_buf[1]; float& a5 = pos_buf[2];
    float& a6 = vel_buf[0]; float& a7 = vel_buf[1]; float& a8 = vel_buf[2];

    const float kSkin = 0.000445f;
    float v79 = vec3_len_sq(vel_buf);

    if (v79 == 0.0f) {
        a2_out[0] = a3;
        a2_out[1] = a4;
        a2_out[2] = a5;
        return a2_out;
    }

    float v114_buf[3] = { a3 + a6, a4 + a7, a5 + a8 };

    if (*a9_depth >= *fU(o, 1712)) {
        a2_out[0] = a3;
        a2_out[1] = a4;
        a2_out[2] = a5;
        return a2_out;
    }
    ++*a9_depth;

    *fB(o, 1616) = 0;
    *fD(o, 1624) = -1.0; // default friction qword_13C43E8

    float v104_buf[3] = {
        *fF(o, 1680) * a3,
        *fF(o, 1684) * a4,
        *fF(o, 1688) * a5
    };

    const float a6_real = a6, a7_real = a7, a8_real = a8;
    {
        constexpr float kMinSweep = 0.08f;
        float len = vec3_len(vel_buf);
        if (len > 0.0f && len < kMinSweep) {
            float s = kMinSweep / len;
            a6 *= s; a7 *= s; a8 *= s;
        }
    }

    float v92_buf[3] = {
        *fF(o, 1680) * (a3 + a6),
        *fF(o, 1684) * (a4 + a7),
        *fF(o, 1688) * (a5 + a8)
    };

    SAABBLayout v122_obj{};
    v122_obj.vt = S4(0x013F617C); // CoreLib::SAABB::vftable
    v122_obj.min_x = v104_buf[0] - *fF(o, 1680);
    v122_obj.min_y = v104_buf[1] - *fF(o, 1684);
    v122_obj.min_z = v104_buf[2] - *fF(o, 1688);
    v122_obj.max_x = v104_buf[0] + *fF(o, 1680);
    v122_obj.max_y = v104_buf[1] + *fF(o, 1684);
    v122_obj.max_z = v104_buf[2] + *fF(o, 1688);

    SAABBLayout v34_obj{};
    v34_obj.vt = S4(0x013F617C);
    v34_obj.min_x = v92_buf[0] - *fF(o, 1680);
    v34_obj.min_y = v92_buf[1] - *fF(o, 1684);
    v34_obj.min_z = v92_buf[2] - *fF(o, 1688);
    v34_obj.max_x = v92_buf[0] + *fF(o, 1680);
    v34_obj.max_y = v92_buf[1] + *fF(o, 1684);
    v34_obj.max_z = v92_buf[2] + *fF(o, 1688);

    saabb_union(&v122_obj, &v34_obj);

    float convar_2f = ReadConvar(0x2Fu);
    v122_obj.min_x -= convar_2f;
    v122_obj.min_y -= convar_2f;
    v122_obj.min_z -= convar_2f;
    v122_obj.max_x += convar_2f;
    v122_obj.max_y += convar_2f;
    v122_obj.max_z += convar_2f;

    uint32_t mask = *fU(o, 1704);
    uint32_t coll_mask = GetCollisionMask();

    if ((mask & coll_mask) != 0) {
        void* bcast_this = *reinterpret_cast<void**>(S4(0x01644AD8));
        if (bcast_this && (uintptr_t)bcast_this > 0x10000) {
            typedef int (__thiscall* FnBroadcast)(void*, uint32_t, void*, float*, float*, void*, void*);
            auto fnBroadcast = reinterpret_cast<FnBroadcast>(S4(0x007B5030));
            fnBroadcast(bcast_this,
                coll_mask & mask,
                &v122_obj,
                &a3,
                &a6,
                reinterpret_cast<void*>(o + 1680),
                reinterpret_cast<void*>(o + 8));
        }
    }
    if ((mask & 0x1000) != 0) {
        typedef int (__cdecl* FnSlope)(void*, float*, float*, void*, void*);
        auto fnSlope = reinterpret_cast<FnSlope>(S4(0x007B24A0));
        fnSlope(&v122_obj, &a3, &a6, reinterpret_cast<void*>(o + 1680), o);
    }
    if ((mask & 0x10000) != 0) {
        typedef int (__cdecl* FnStep)(void*, float*, float*, void*, void*);
        auto fnStep = reinterpret_cast<FnStep>(S4(0x007B2C90));
        fnStep(&v122_obj, &a3, &a6, reinterpret_cast<void*>(o + 1680), o);
    }
    if ((mask & 0x200000) != 0) {
        typedef int (__cdecl* FnEdge)(void*, float*, float*, void*, void*);
        auto fnEdge = reinterpret_cast<FnEdge>(S4(0x007B3520));
        fnEdge(&v122_obj, &a3, &a6, reinterpret_cast<void*>(o + 1680), o);
    }

    a6 = a6_real; a7 = a7_real; a8 = a8_real;

    if (!*fB(o, 1616)) {
        float total_len = vec3_len(vel_buf);
        float scale = 1.0f;
        if (total_len > kSkin) {
            scale = (total_len - kSkin) / total_len;
        }
        a2_out[0] = a3 + a6 * scale;
        a2_out[1] = a4 + a7 * scale;
        a2_out[2] = a5 + a8 * scale;
        return a2_out;
    }

    float v149 = a3, v150 = a4, v151 = a5;
    float coll_eps = GetCollisionEpsilon();

    if (*fD(o, 1624) <= (double)coll_eps) {
        float wall = ReadConvar(1u);
        bool blocked = (wall > *fD(o, 1624));
        if (blocked && *fI(o, *fI(o, 8) * 4 + 12) != 4096) {
            if (*fI(o, 1720) == 1)
                *fB(o, 1716) = 1;
            *fD(o, 1624) = 0.0;
            a2_out[0] = v149;
            a2_out[1] = v150;
            a2_out[2] = v151;
            return a2_out;
        }
    }
    else {
        float len = vec3_len(vel_buf);
        if (len > 0.0f) {
            float ratio = (float)(*fD(o, 1624) - (double)coll_eps) / len;
            v149 += a6 * ratio;
            v150 += a7 * ratio;
            v151 += a8 * ratio;
        }
    }

    vec3_norm(vel_buf);

    if (*fI(o, *fI(o, 8) * 4 + 12) == 4096) {
        if (*fI(o, 1720) == 1) {
            bool stop;
            if (*a9_depth > 1u) {
                stop = true;
            }
            else {
                float t = ReadConvar(0xDu);
                stop = (t > (v150 - *fF(o, 1636)));
            }
            if (stop) {
                *fB(o, 1716) = 1;
                a2_out[0] = v149;
                a2_out[1] = v150;
                a2_out[2] = v151;
                return a2_out;
            }
        }

        if ((*fU(o, 1708) & 0x1000) != 0) {
            float v149_pt[3] = { v149, v150, v151 };
            float scl = vec3_dist(v114_buf, v149_pt);
            a6 *= scl; a7 *= scl; a8 *= scl;
            float v98 = *fF(o, 1644);
            float v99 = *fF(o, 1648);
            float v100 = *fF(o, 1652);
            float norm[3] = { v98, v99, v100 };
            float dotp = vec3_dot(vel_buf, norm);
            float v44[3] = { v98 * dotp, v99 * dotp, v100 * dotp };
            float v74 = a6 - v44[0];
            float v75 = a7 - v44[1];
            float v76 = a8 - v44[2];
            if (*fI(o, 1720) == 1 && v75 > 0.0f)
                v75 = 0.0f;
            HookCollisionResolverInternal(o, 0, a2_out, v149, v150, v151,
                v74, v75, v76, a9_depth);
            return a2_out;
        }

        a2_out[0] = v149;
        a2_out[1] = v150;
        a2_out[2] = v151;
        return a2_out;
    }

    bool slide_label = false;
    if (*fI(o, 1720) == 1) {
        bool stop;
        if (*a9_depth > 1u) {
            stop = true;
        }
        else {
            float t = ReadConvar(0x2Au);
            stop = ((v150 - *fF(o, 1636)) > t);
        }
        if (stop) {
            *fB(o, 1716) = 1;
            a2_out[0] = v149;
            a2_out[1] = v150;
            a2_out[2] = v151;
            return a2_out;
        }

        float v102 = *fF(o, 1636) + coll_eps;
        float v33[3] = {
            *fF(o, 1680) * v149,
            *fF(o, 1684) * v102,
            *fF(o, 1688) * v151
        };
        float v107 = 0.0f;
        void* sweep_this = *reinterpret_cast<void**>(S4(0x01644AD8));
        char sweep_hit = 0;
        if (sweep_this && (uintptr_t)sweep_this > 0x10000) {
            typedef char (__thiscall* FnBcastBool)(void*, uint32_t, void*, float*);
            auto fnBcastBool = reinterpret_cast<FnBcastBool>(S4(0x007B5AE0));
            sweep_hit = fnBcastBool(sweep_this,
                coll_mask & *fU(o, 1704),
                v33, &v107);
        }
        if (!sweep_hit) {
            slide_label = true;
        }
        else {
            float t = ReadConvar(0x25u);
            bool over = (t > ((v33[1] - v107) / *fF(o, 1684)));
            if (over) {
                *fB(o, 1716) = 1;
                a2_out[0] = v149;
                a2_out[1] = v150;
                a2_out[2] = v151;
                return a2_out;
            }
            slide_label = true;
        }
    }
    else {
        slide_label = true;
    }

    if (slide_label) {
        if ((*fU(o, 1708) & coll_mask) != 0) {
            float v95 = *fF(o, 1632);
            float v96 = *fF(o, 1636);
            float v97 = *fF(o, 1640);
            float v101[3] = {
                v149 - v95,
                v150 - v96,
                v151 - v97
            };
            vec3_norm(v101);
            float vdotN = vec3_dot(v101, v114_buf);
            float v95_vec[3] = { v95, v96, v97 };
            float vdotS = vec3_dot(v101, v95_vec);
            float diff = vdotN - vdotS;
            float v48[3] = { v101[0] * diff, v101[1] * diff, v101[2] * diff };
            float v47[3] = { v114_buf[0] - v48[0], v114_buf[1] - v48[1], v114_buf[2] - v48[2] };
            float v89[3] = { v47[0] - v95, v47[1] - v96, v47[2] - v97 };

            float remaining = a6 * v89[0] + a7 * v89[1] + a8 * v89[2];
            if (0.0f <= remaining) {
                if (*fI(o, 1720) == 1 && v89[1] > 0.0f) {
                    v89[1] = 0.0f;
                    *fB(o, 1716) = 1;
                    a2_out[0] = v149;
                    a2_out[1] = v150;
                    a2_out[2] = v151;
                    return a2_out;
                }
                HookCollisionResolverInternal(o, 0, a2_out, v149, v150, v151,
                    v89[0], v89[1], v89[2], a9_depth);
                return a2_out;
            }
            a2_out[0] = v149;
            a2_out[1] = v150;
            a2_out[2] = v151;
            return a2_out;
        }

        if (*fI(o, 1720) == 1) {
            *fB(o, 1716) = 1;
        }
        a2_out[0] = v149;
        a2_out[1] = v150;
        a2_out[2] = v151;
        return a2_out;
    }

    a2_out[0] = v149;
    a2_out[1] = v150;
    a2_out[2] = v151;
    return a2_out;
}

static float* __fastcall Hook_CollisionResolver(
    char* self, int edx,
    float* a2_out,
    float a3_in, float a4_in, float a5_in,
    float a6_in, float a7_in, float a8_in,
    uint32_t* a9_depth)
{
    return HookCollisionResolverInternal(self, edx, a2_out, a3_in, a4_in, a5_in, a6_in, a7_in, a8_in, a9_depth);
}

void InstallCollisionResolverFix()
{
    static bool installed = false;
    if (installed) return;
    installed = true;

    OriginalCollisionResolver = reinterpret_cast<FnCollisionResolver>(S4(0x007B11C0));
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID*>(&OriginalCollisionResolver), Hook_CollisionResolver);
    DetourTransactionCommit();
}
