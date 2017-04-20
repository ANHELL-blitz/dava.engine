#pragma once

#include "Base/BaseTypes.h"
#include <atomic>

namespace DAVA
{
class RenderSystem;
class RenderObject;
class RenderBatch;
class Texture;
class GeoDecalManager
{
public:
    enum Mapping : uint32
    {
        PLANAR,
        SPHERICAL,
        CYLINDRICAL,
        COUNT
    };

    struct DecalConfig
    {
        AABBox3 boundingBox;
        Matrix4 worldTransform;
        Mapping mapping = Mapping::PLANAR;
        FilePath image;

        bool operator==(const DecalConfig&) const;
        bool operator!=(const DecalConfig&) const;
        void invalidate();
    };

    using Decal = struct
    {
        uint32 key = 0xDECAAAAA;
    }*;

public:
    GeoDecalManager(RenderSystem* renderSystem);

    Decal BuildDecal(const DecalConfig& config, RenderObject* object);
    void DeleteDecal(Decal decal);

private:
    // this could be moved to public
    void RegisterDecal(Decal decal);
    void UnregisterDecal(Decal decal);

    struct DecalVertex;
    struct DecalBuildInfo;

    struct BuiltDecal
    {
        RefPtr<RenderObject> sourceObject = RefPtr<RenderObject>(nullptr);
        RefPtr<RenderObject> renderObject = RefPtr<RenderObject>(nullptr);
        bool registered = false;
    };

    bool BuildDecal(const DecalBuildInfo& info, RenderBatch* dstBatch);

    void ClipToPlane(DecalVertex* p_vs, uint8_t* nb_p_vs, int8_t sign, Vector3::eAxis axis, const Vector3& c_v);
    void ClipToBoundingBox(DecalVertex* p_vs, uint8_t* nb_p_vs, const AABBox3& clipper);
    int8_t Classify(int8_t sign, Vector3::eAxis axis, const Vector3& c_v, const DecalVertex& p_v);
    void Lerp(float t, const DecalVertex& v1, const DecalVertex& v2, DecalVertex& result);

private:
    RenderSystem* renderSystem = nullptr;
    Texture* defaultNormalMap = nullptr;
    Map<Decal, BuiltDecal> builtDecals;
    std::atomic<uintptr_t> decalCounter{ 0 };
};

inline bool GeoDecalManager::DecalConfig::operator==(const GeoDecalManager::DecalConfig& r) const
{
    return (boundingBox == r.boundingBox) &&
    (image == r.image) &&
    (mapping == r.mapping);
}

inline bool GeoDecalManager::DecalConfig::operator!=(const GeoDecalManager::DecalConfig& r) const
{
    return (boundingBox != r.boundingBox) ||
    (image != r.image) ||
    (mapping != r.mapping);
}

inline void GeoDecalManager::DecalConfig::invalidate()
{
    boundingBox.Empty();
    image = FilePath();
}
}