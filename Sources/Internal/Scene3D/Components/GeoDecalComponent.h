#pragma once

#include "Base/BaseTypes.h"
#include "Reflection/Reflection.h"
#include "Debug/DVAssert.h"
#include "Entity/Component.h"
#include "Base/Introspection.h"
#include "Scene3D/SceneFile/SerializationContext.h"
#include "Render/Highlevel/GeoDecalManager.h"

namespace DAVA
{
class GeoDecalComponent : public Component
{
public:
    IMPLEMENT_COMPONENT_TYPE(GEO_DECAL_COMPONENT);

    GeoDecalComponent() = default;

    Component* Clone(Entity* toEntity) override;
    void Serialize(KeyedArchive* archive, SerializationContext* serializationContext) override;
    void Deserialize(KeyedArchive* archive, SerializationContext* serializationContext) override;

    const GeoDecalManager::DecalConfig& GetConfig() const;

private:
    GeoDecalManager::DecalConfig config;

public:
#define IMPL_PROPERTY(T, Name, varName) \
    const T& Get##Name() const { return config.varName; } \
    void Set##Name(const T& value) { config.varName = value; }
    IMPL_PROPERTY(FilePath, DecalImage, image);
    IMPL_PROPERTY(AABBox3, BoundingBox, boundingBox);
#undef IMPL_PROPERTY

    uint32 GetMapping() const;
    void SetMapping(uint32 value);

    INTROSPECTION_EXTEND(GeoDecalComponent, Component,
                         PROPERTY("Bounding Box", "Bounding Box", GetBoundingBox, SetBoundingBox, I_VIEW | I_EDIT | I_SAVE)
                         PROPERTY("Decal image", "Decal image", GetDecalImage, SetDecalImage, I_VIEW | I_EDIT | I_SAVE)
                         PROPERTY("Texture mapping", InspDesc("Texture mapping", GlobalEnumMap<GeoDecalManager::Mapping>::Instance()), GetMapping, SetMapping, I_SAVE | I_VIEW | I_EDIT)
                         )
    DAVA_VIRTUAL_REFLECTION(GeoDecalComponent, Component);
};

inline uint32 GeoDecalComponent::GetMapping() const
{
    return config.mapping;
}

inline void GeoDecalComponent::SetMapping(uint32 value)
{
    DVASSERT(value < GeoDecalManager::Mapping::COUNT);
    config.mapping = static_cast<GeoDecalManager::Mapping>(value);
}

inline const GeoDecalManager::DecalConfig& GeoDecalComponent::GetConfig() const
{
    return config;
}


}
