#include "HeapProbe.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include <spdlog/spdlog.h>

#include "Engine/Renderer/RenderManager.h"

// Stack symbolisation for the huge-allocation hook (temporary diagnostic).
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

std::string g_heapProbeGltfAsset = "content/mesh/player/weapon/knife_animated.glb";

// ---------------------------------------------------------------------------
// TEMPORARY global operator new/delete replacement.
//
// The SmallVector::reserve and parseAttributes guards both came back silent, so
// the huge allocation is being requested by something else entirely. This hooks
// EVERY allocation in the exe (DLLs with their own CRT are unaffected) and
// reports any request over the threshold, with the size that will then throw
// std::bad_alloc. Delete this block once the culprit is identified.
// ---------------------------------------------------------------------------
#if ENGINE_HEAP_PROBE

namespace
{
    constexpr std::size_t kHugeAllocBytes = 64ull * 1024 * 1024; // 64 MB

    // Symbolised stack of whoever requested the absurd allocation. dbghelp
    // itself allocates, so a recursion guard is mandatory here.
    void dumpHugeAllocStack()
    {
        static thread_local bool s_inside = false;
        if (s_inside)
            return;
        s_inside = true;

        static bool s_symInit = false;
        if (!s_symInit)
        {
            s_symInit = true;
            SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
            SymInitialize(GetCurrentProcess(), nullptr, TRUE);
        }

        void* frames[32] = {};
        const USHORT n = CaptureStackBackTrace(2, 32, frames, nullptr);

        char symBuf[sizeof(SYMBOL_INFO) + 512] = {};
        SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = 500;

        for (USHORT i = 0; i < n; i++)
        {
            const DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
            DWORD64 disp = 0;
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisp = 0;

            if (SymFromAddr(GetCurrentProcess(), addr, &disp, sym))
            {
                if (SymGetLineFromAddr64(GetCurrentProcess(), addr, &lineDisp, &line))
                    std::fprintf(stderr, "    [%2u] %s  (%s:%lu)\n", i, sym->Name,
                        line.FileName ? line.FileName : "?", line.LineNumber);
                else
                    std::fprintf(stderr, "    [%2u] %s\n", i, sym->Name);
            }
            else
            {
                std::fprintf(stderr, "    [%2u] 0x%llX\n", i, (unsigned long long)addr);
            }
        }
        std::fflush(stderr);
        s_inside = false;
    }

    inline void reportHugeAlloc(std::size_t n, const char* which)
    {
        std::fprintf(stderr, "HUGE ALLOC via %s: %llu bytes (0x%llX) = %.1f MB\n",
            which, (unsigned long long)n, (unsigned long long)n,
            (double)n / (1024.0 * 1024.0));
        std::fflush(stderr);
        dumpHugeAllocStack();
    }

    inline void* allocOrNull(std::size_t n, const char* which)
    {
        if (n > kHugeAllocBytes)
            reportHugeAlloc(n, which);
        return std::malloc(n ? n : 1);
    }
}

void* operator new(std::size_t n)
{
    void* p = allocOrNull(n, "new");
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n)
{
    void* p = allocOrNull(n, "new[]");
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept   { return allocOrNull(n, "new(nothrow)"); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return allocOrNull(n, "new[](nothrow)"); }

void operator delete(void* p) noexcept   { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept   { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept   { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

#endif // ENGINE_HEAP_PROBE

namespace
{
    // Each canary word is self-identifying, so a partial overwrite still tells
    // us exactly which word was hit and what the original value should be.
    constexpr uint64_t kMagic = 0xC0DEFACE00000000ull;

    inline uint64_t expectedWord(uint64_t globalWordIndex)
    {
        return kMagic | (globalWordIndex & 0xFFFFFFFFull);
    }

    struct Generation
    {
        std::string label;
        std::vector<uint64_t*> blocks;
        std::size_t wordsPerBlock = 0;
        uint64_t firstWordIndex = 0;
    };

    std::vector<Generation> g_generations;
    uint64_t g_nextWordIndex = 0;

    // Largest single allocation operator new will still satisfy, in MB.
    // Distinguishes a genuinely exhausted heap from corrupted/garbage counts:
    // if this stays high while fastgltf throws bad_alloc, the heap is FINE and
    // the count it reserved on was nonsense.
    std::size_t allocHeadroomMB()
    {
        // malloc, not new: this must not trip the huge-allocation hook below.
        std::size_t best = 0;
        for (std::size_t mb : { 1u, 8u, 64u, 256u, 1024u })
        {
            void* p = std::malloc(mb * 1024u * 1024u);
            if (!p)
                break;
            std::free(p);
            best = mb;
        }
        return best;
    }

    // A clobbered value usually identifies its writer. Classify it.
    std::string describeValue(uint64_t v)
    {
        std::string out;

        // x64 user-space pointers live below 0x0000_8000_0000_0000 and are
        // never tiny; heap/code pointers are the most common corruptor.
        if (v > 0x10000ull && v < 0x0000800000000000ull)
            out += " [looks like a user-space POINTER]";

        // Two packed floats — vertex/transform data scribbling past its end
        float f[2];
        std::memcpy(f, &v, sizeof(f));
        auto sane = [](float x) { return x == x && x != 0.0f && x > -1e7f && x < 1e7f; };
        if (sane(f[0]) || sane(f[1]))
        {
            out += " [as 2 floats: " + std::to_string(f[0]) + ", " + std::to_string(f[1]) + "]";
        }

        // Printable ASCII — a string copy overrunning
        char c[9] = {};
        std::memcpy(c, &v, 8);
        bool printable = true;
        for (int i = 0; i < 8; i++)
            if (c[i] != 0 && (c[i] < 0x20 || c[i] > 0x7E)) { printable = false; break; }
        if (printable && c[0] != 0)
            out += std::string(" [as ASCII: \"") + c + "\"]";

        if (v == 0)
            out += " [zeroed - freed/memset overrun]";

        return out;
    }
}

namespace HeapProbe
{

void arm(const char* label, std::size_t blocks, std::size_t blockBytes)
{
    // This is a WinMain app: stderr goes nowhere, which is why the patched
    // fastgltf guards have been invisible this whole time. Point it at a file
    // so their diagnostics (including the bogus reserve counts) are visible.
    static bool s_stderrRedirected = false;
    if (!s_stderrRedirected)
    {
        s_stderrRedirected = true;
        if (std::freopen("log/fastgltf_stderr.log", "w", stderr))
            spdlog::info("[heapprobe] stderr redirected to log/fastgltf_stderr.log");
    }

    Generation gen;
    gen.label         = label ? label : "?";
    gen.wordsPerBlock = blockBytes / sizeof(uint64_t);
    gen.firstWordIndex = g_nextWordIndex;
    gen.blocks.reserve(blocks);

    for (std::size_t b = 0; b < blocks; b++)
    {
        uint64_t* block = new (std::nothrow) uint64_t[gen.wordsPerBlock];
        if (!block)
            break;
        for (std::size_t w = 0; w < gen.wordsPerBlock; w++)
            block[w] = expectedWord(g_nextWordIndex + w);
        g_nextWordIndex += gen.wordsPerBlock;
        gen.blocks.push_back(block);
    }

    spdlog::info("[heapprobe] armed '{}': {} blocks x {} KB ({:.1f} MB total), first block at {}",
        gen.label, gen.blocks.size(), blockBytes / 1024,
        (double)(gen.blocks.size() * blockBytes) / (1024.0 * 1024.0),
        gen.blocks.empty() ? (void*)nullptr : (void*)gen.blocks.front());

    g_generations.push_back(std::move(gen));
}

int check(const char* label)
{
    int damaged = 0;

    for (const Generation& gen : g_generations)
    {
        for (std::size_t b = 0; b < gen.blocks.size(); b++)
        {
            uint64_t* block = gen.blocks[b];
            const uint64_t base = gen.firstWordIndex + b * gen.wordsPerBlock;

            for (std::size_t w = 0; w < gen.wordsPerBlock; w++)
            {
                const uint64_t want = expectedWord(base + w);
                if (block[w] == want)
                    continue;

                ++damaged;
                spdlog::critical(
                    "[heapprobe] CORRUPTION at '{}' | generation '{}' block {} word {} | "
                    "address {} | expected 0x{:016X} got 0x{:016X}{}",
                    label ? label : "?", gen.label, b, w,
                    (void*)&block[w], want, block[w], describeValue(block[w]));

                // Report how far the damage runs — a long run means a memcpy /
                // buffer overrun, a single word means a stray pointer write.
                std::size_t run = 0;
                for (std::size_t k = w; k < gen.wordsPerBlock; k++)
                {
                    if (block[k] == expectedWord(base + k))
                        break;
                    ++run;
                }
                spdlog::critical("[heapprobe]   damage run: {} word(s) = {} bytes",
                    run, run * sizeof(uint64_t));
                break; // one report per block is enough
            }
        }
    }

    if (damaged == 0)
        spdlog::info("[heapprobe] check '{}': clean ({} generation(s))", label ? label : "?", g_generations.size());
    else
        spdlog::critical("[heapprobe] check '{}': {} DAMAGED BLOCK(S)", label ? label : "?", damaged);

    return damaged;
}

bool gltfProbe(const char* label, const std::string& path)
{
    auto* rm = RenderManager::Get();
    if (!rm || !rm->gltf() || !rm->sceneManager())
    {
        spdlog::warn("[heapprobe] gltf probe '{}': renderer not ready, skipped", label ? label : "?");
        return true;
    }

    // Evict first so we genuinely re-parse rather than hitting the mesh cache.
    auto* cache = rm->sceneManager()->getMeshCache();
    if (irr::scene::IAnimatedMesh* cached = cache->getMeshByName(path.c_str()))
        cache->removeMesh(cached);

    const std::size_t headroomBefore = allocHeadroomMB();

    irr::scene::IAnimatedMesh* mesh = rm->gltf()->getMesh(path.c_str());
    if (!mesh)
    {
        spdlog::critical("[heapprobe] gltf probe '{}': FAILED (alloc headroom before/after: {} MB / {} MB) - {}",
            label ? label : "?", headroomBefore, allocHeadroomMB(), rm->gltf()->getError());
        return false;
    }

    spdlog::info("[heapprobe] gltf probe '{}': ok (alloc headroom {} MB)",
        label ? label : "?", headroomBefore);
    cache->removeMesh(mesh);
    return true;
}

void release()
{
    for (Generation& gen : g_generations)
        for (uint64_t* block : gen.blocks)
            delete[] block;
    g_generations.clear();
    g_nextWordIndex = 0;
}

} // namespace HeapProbe
