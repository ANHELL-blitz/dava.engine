#include "MotionState.h"
#include "MotionTransition.h"

#include "FileSystem/YamlNode.h"
#include "Scene3D/SkeletonAnimation/BlendTree.h"

namespace DAVA
{
MotionState::~MotionState()
{
    SafeDelete(blendTree);
}

void MotionState::Reset()
{
    rootOffset = Vector3();
    animationPrevPhaseIndex = 0;
    animationCurrPhaseIndex = 0;
    animationPhase = 0.f;
}

void MotionState::Update(float32 dTime)
{
    if (blendTree == nullptr)
        return;

    reachedMarkers.clear();
    reachedMarkersSet.clear();
    animationEndReached = false;

    animationPrevPhaseIndex = animationCurrPhaseIndex;

    float32 animationPhase0 = animationPhase;
    uint32 animationCurrPhaseIndex0 = animationCurrPhaseIndex;

    float32 duration = blendTree->EvaluatePhaseDuration(animationCurrPhaseIndex, boundParams);
    animationPhase += (duration != 0.f) ? (dTime / duration) : 0.f;
    if (animationPhase >= 1.f)
    {
        animationPhase -= 1.f;

        const FastName& phaseMarker = markers[animationCurrPhaseIndex];
        if (phaseMarker.IsValid())
        {
            reachedMarkers.push_back(phaseMarker);
            reachedMarkersSet.insert(phaseMarker);
        }

        ++animationCurrPhaseIndex;
        if (animationCurrPhaseIndex == blendTree->GetPhasesCount())
        {
            animationCurrPhaseIndex = 0;
            animationEndReached = true;
        }

        float32 nextPhaseDuration = blendTree->EvaluatePhaseDuration(animationCurrPhaseIndex, boundParams);
        animationPhase = (nextPhaseDuration != 0.f) ? (animationPhase * duration / nextPhaseDuration) : animationPhase;
        animationPhase = Clamp(animationPhase, 0.f, 1.f);
    }

    blendTree->EvaluateRootOffset(animationCurrPhaseIndex0, animationPhase0, animationCurrPhaseIndex, animationPhase, boundParams, &rootOffset);
}

void MotionState::EvaluatePose(SkeletonPose* outPose) const
{
    if (blendTree != nullptr)
        blendTree->EvaluatePose(animationCurrPhaseIndex, animationPhase, boundParams, outPose);
}

void MotionState::GetRootOffsetDelta(Vector3* offset) const
{
    *offset = rootOffset;
}

void MotionState::SyncPhase(const MotionState* other, const MotionTransitionInfo* transitionInfo)
{
    DVASSERT(transitionInfo != nullptr);

    const Vector<uint32>& phaseMap = transitionInfo->phaseMap;
    if (!phaseMap.empty())
    {
        animationPrevPhaseIndex = (other->animationPrevPhaseIndex < phaseMap.size()) ? phaseMap[other->animationPrevPhaseIndex] : animationPrevPhaseIndex;
        animationCurrPhaseIndex = (other->animationCurrPhaseIndex < phaseMap.size()) ? phaseMap[other->animationCurrPhaseIndex] : animationCurrPhaseIndex;

        DVASSERT(animationPrevPhaseIndex < blendTree->GetPhasesCount());
        DVASSERT(animationCurrPhaseIndex < blendTree->GetPhasesCount());
    }

    if (transitionInfo->syncPhase)
    {
        animationPhase = other->animationPhase;
    }

    if (transitionInfo->inversePhase)
    {
        animationPhase = 1.f - animationPhase;
    }
}

const Vector<FastName>& MotionState::GetBlendTreeParameters() const
{
    static Vector<FastName> empty;
    return (blendTree != nullptr) ? blendTree->GetParameterIDs() : empty;
}

void MotionState::BindSkeleton(const SkeletonComponent* skeleton)
{
    if (blendTree != nullptr)
        blendTree->BindSkeleton(skeleton);
}

void MotionState::BindRootNode(const FastName& rootNodeID)
{
    if (blendTree != nullptr)
        blendTree->BindRootNode(rootNodeID);
}

bool MotionState::BindParameter(const FastName& parameterID, const float32* param)
{
    bool success = false;
    if (blendTree != nullptr)
    {
        const Vector<FastName>& params = blendTree->GetParameterIDs();
        auto found = std::find(params.begin(), params.end(), parameterID);
        if (found != params.end())
        {
            size_t paramIndex = std::distance(params.begin(), found);
            boundParams[paramIndex] = param;
            success = true;
        }
    }

    return success;
}

void MotionState::UnbindParameters()
{
    std::fill(boundParams.begin(), boundParams.end(), nullptr);
}

void MotionState::AddTransition(const FastName& trigger, MotionTransitionInfo* transitionInfo, MotionState* dstState, uint32 srcPhase)
{
    DVASSERT(transitionInfo != nullptr);
    DVASSERT(dstState != nullptr);

    TransitionKey key(trigger, srcPhase);
    DVASSERT(transitions.count(key) == 0);

    transitions[key] = { transitionInfo, dstState };
}

const MotionState::TransitionInfo& MotionState::GetTransitionInfo(const FastName& trigger) const
{
    auto found = transitions.find(TransitionKey(trigger, animationCurrPhaseIndex));
    if (found != transitions.end())
        return found->second;

    found = transitions.find(TransitionKey(trigger));
    if (found != transitions.end())
        return found->second;

    static TransitionInfo emptyInfo = { nullptr, nullptr };
    return emptyInfo;
}

void MotionState::LoadFromYaml(const YamlNode* stateNode)
{
    DVASSERT(stateNode);

    const YamlNode* stateIDNode = stateNode->Get("state-id");
    if (stateIDNode != nullptr && stateIDNode->GetType() == YamlNode::TYPE_STRING)
    {
        id = stateIDNode->AsFastName();
    }

    const YamlNode* blendTreeNode = stateNode->Get("blend-tree");
    if (blendTreeNode != nullptr)
    {
        blendTree = BlendTree::LoadFromYaml(blendTreeNode);
        DVASSERT(blendTree != nullptr);

        boundParams.resize(blendTree->GetParameterIDs().size());
        markers.resize(blendTree->GetPhasesCount());

        const YamlNode* markersNode = blendTreeNode->Get("markers");

        if (markersNode != nullptr && markersNode->GetType() == YamlNode::TYPE_ARRAY)
        {
            uint32 markersCount = markersNode->GetCount();
            for (uint32 m = 0; m < markersCount; ++m)
            {
                const YamlNode* markerNode = markersNode->Get(m);
                if (markerNode->GetType() == YamlNode::TYPE_MAP && markerNode->GetCount() == 1)
                {
                    uint32 markerIndex = 0;
                    if (sscanf(markerNode->GetItemKeyName(0).c_str(), "%u", &markerIndex) == 1)
                    {
                        DVASSERT(markerIndex < uint32(markers.size()));
                        markers[markerIndex] = markerNode->Get(0)->AsFastName();
                    }
                }
            }
        }
    }
}

} //ns