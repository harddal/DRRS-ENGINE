#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>

#include "irrlicht.h"

// CPU + GPU splat map for texture painting.
// RGBA channels map to blend layers A/B/C/D.
// All pixels start at 0 so the original mesh diffuse shows through at full weight.
struct SplatMap
{
    int                             resolution = 512;
    std::vector<irr::video::SColor> pixels;     // ARGB32, row-major
    irr::video::ITexture*           gpuTexture  = nullptr;
    std::string                     savePath;

    void create(int res, irr::video::IVideoDriver* driver, const std::string& name)
    {
        resolution = res;
        pixels.assign(res * res, irr::video::SColor(0, 0, 0, 0));
        gpuTexture = driver->addTexture(
            irr::core::dimension2d<irr::u32>(res, res),
            name.c_str(),
            irr::video::ECF_A8R8G8B8);
        uploadToGPU();
    }

    // Paint a smoothstep-falloff circle into one channel (0=R, 1=G, 2=B, 3=A).
    // strength is [0,1], radiusPx is in pixel space.
    void paint(int cx, int cy, int channel, float strength, float radiusPx)
    {
        int r = static_cast<int>(ceilf(radiusPx));
        for (int dy = -r; dy <= r; ++dy)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                int px = cx + dx;
                int py = cy + dy;
                if (px < 0 || px >= resolution || py < 0 || py >= resolution) continue;

                float dist = sqrtf(static_cast<float>(dx * dx + dy * dy));
                if (dist > radiusPx) continue;

                float t     = 1.0f - (dist / radiusPx);
                t           = t * t * (3.0f - 2.0f * t); // smoothstep
                irr::u32 add = static_cast<irr::u32>(t * strength * 255.f);

                irr::video::SColor& c = pixels[py * resolution + px];
                switch (channel)
                {
                    case 0: c.setRed  (std::min(c.getRed()   + add, 255u)); break;
                    case 1: c.setGreen(std::min(c.getGreen() + add, 255u)); break;
                    case 2: c.setBlue (std::min(c.getBlue()  + add, 255u)); break;
                    case 3: c.setAlpha(std::min(c.getAlpha() + add, 255u)); break;
                }
            }
        }
    }

    void uploadToGPU()
    {
        if (!gpuTexture) return;
        void* data = gpuTexture->lock(irr::video::ETLM_WRITE_ONLY);
        if (!data) return;
        std::memcpy(data, pixels.data(), pixels.size() * sizeof(irr::video::SColor));
        gpuTexture->unlock();
    }

    void save(irr::video::IVideoDriver* driver)
    {
        if (savePath.empty() || pixels.empty()) return;
        irr::video::IImage* img = driver->createImageFromData(
            irr::video::ECF_A8R8G8B8,
            irr::core::dimension2d<irr::u32>(resolution, resolution),
            pixels.data(),
            false);
        if (img)
        {
            driver->writeImageToFile(img, savePath.c_str());
            img->drop();
        }
    }

    bool load(const std::string& path, irr::video::IVideoDriver* driver)
    {
        savePath = path;
        irr::video::IImage* img = driver->createImageFromFile(path.c_str());
        if (!img) return false;
        resolution = static_cast<int>(img->getDimension().Width);
        pixels.resize(resolution * resolution);
        for (int y = 0; y < resolution; ++y)
            for (int x = 0; x < resolution; ++x)
                pixels[y * resolution + x] = img->getPixel(x, y);
        img->drop();
        return true;
    }
};
