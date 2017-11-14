#include "Classes/EditorSystems/EditorSystemsManager.h"
#include "Classes/EditorSystems/MovableInEditorComponent.h"
#include "Classes/EditorSystems/CounterpoiseComponent.h"

#include "Classes/Modules/DocumentsModule/DocumentData.h"
#include "Classes/Modules/CanvasModule/CanvasModuleData.h"
#include "Classes/Modules/CanvasModule/EditorControlsView.h"
#include "Classes/Modules/DocumentsModule/EditorSystemsData.h"
#include "Classes/Modules/UpdateViewsSystemModule/UpdateViewsSystem.h"

#include "Classes/Model/PackageHierarchy/PackageNode.h"
#include "Classes/Model/PackageHierarchy/PackageControlsNode.h"
#include "Classes/Model/PackageHierarchy/ControlNode.h"

#include "Classes/EditorSystems/SelectionSystem.h"
#include "Classes/EditorSystems/EditorTransformSystem.h"
#include "Classes/EditorSystems/CursorSystem.h"

#include <TArc/Core/ContextAccessor.h>
#include <TArc/Core/FieldBinder.h>
#include <TArc/DataProcessing/DataContext.h>
#include <TArc/Utils/Utils.h>

#include <UI/UIControl.h>
#include <UI/UIControlBackground.h>
#include <UI/Input/UIModalInputComponent.h>
#include <UI/Input/UIInputSystem.h>
#include <UI/UIControlSystem.h>
#include <UI/UIScreen.h>
#include <UI/UIScreenManager.h>
#include <Engine/Engine.h>
#include <Entity/ComponentManager.h>

using namespace DAVA;

EditorSystemsManager::StopPredicate EditorSystemsManager::defaultStopPredicate = [](const ControlNode*) { return false; };

EditorSystemsManager::EditorSystemsManager(DAVA::TArc::ContextAccessor* accessor_)
    : rootControl(new UIControl())
    , accessor(accessor_)
{
    using namespace DAVA;
    using namespace TArc;

    rootControl->SetName(FastName("root_control"));

    activeAreaChanged.Connect(this, &EditorSystemsManager::OnActiveHUDAreaChanged);

    InitDAVAScreen();

    InitFieldBinder();

    UpdateViewsSystem* updateSystem = DAVA::GetEngineContext()->uiControlSystem->GetSystem<UpdateViewsSystem>();
    updateSystem->beforeRender.Connect(this, &EditorSystemsManager::OnUpdate);

    DAVA_REFLECTION_REGISTER_CUSTOM_PERMANENT_NAME(MovableInEditorComponent, "Movable in editor component");
    GetEngineContext()->componentManager->RegisterComponent<MovableInEditorComponent>();

    DAVA_REFLECTION_REGISTER_CUSTOM_PERMANENT_NAME(CounterpoiseComponent, "Counterpoise component");
    GetEngineContext()->componentManager->RegisterComponent<CounterpoiseComponent>();
}

EditorSystemsManager::~EditorSystemsManager()
{
    const EngineContext* engineContext = GetEngineContext();
    engineContext->uiScreenManager->ResetScreen();

    //TODO: uncomment this line when all systems will be added outside
    //DVASSERT(systems.empty());

    //TODO: remove this block when all systems will be added outside
    for (auto& orderAndSystem : systems)
    {
        delete orderAndSystem.second;
    }

    UpdateViewsSystem* updateSystem = DAVA::GetEngineContext()->uiControlSystem->GetSystem<UpdateViewsSystem>();
    if (updateSystem != nullptr)
    {
        updateSystem->beforeRender.Disconnect(this);
    }
}

void EditorSystemsManager::InitSystems()
{
    selectionSystemPtr = new SelectionSystem(accessor);
    RegisterEditorSystem(selectionSystemPtr);
    RegisterEditorSystem(new EditorTransformSystem(accessor));
    RegisterEditorSystem(new CursorSystem(accessor));
}

void EditorSystemsManager::InitFieldBinder()
{
    using namespace DAVA;
    using namespace DAVA::TArc;

    fieldBinder.reset(new FieldBinder(accessor));
    {
        FieldDescriptor fieldDescr;
        fieldDescr.type = ReflectedTypeDB::Get<DocumentData>();
        fieldDescr.fieldName = FastName(DocumentData::displayedRootControlsPropertyName);
        fieldBinder->BindField(fieldDescr, MakeFunction(this, &EditorSystemsManager::OnRootContolsChanged));
    }
    {
        FieldDescriptor fieldDescr;
        fieldDescr.type = ReflectedTypeDB::Get<DocumentData>();
        fieldDescr.fieldName = FastName(DocumentData::packagePropertyName);
        fieldBinder->BindField(fieldDescr, MakeFunction(this, &EditorSystemsManager::OnPackageChanged));
    }
    {
        FieldDescriptor fieldDescr;
        fieldDescr.type = ReflectedTypeDB::Get<EditorSystemsData>();
        fieldDescr.fieldName = FastName(EditorSystemsData::emulationModePropertyName);
        fieldBinder->BindField(fieldDescr, MakeFunction(this, &EditorSystemsManager::OnEmulationModeChanged));
    }
}

void EditorSystemsManager::OnInput(UIEvent* currentInput)
{
    if (currentInput->device == eInputDevices::MOUSE)
    {
        mouseDelta = currentInput->point - lastMousePos;
        lastMousePos = currentInput->point;
    }

    if (currentInput->device == eInputDevices::MOUSE && currentInput->tapCount > 0)
    {
        // From a series of clicks from mouse we should detect double clicks only.
        // Therefore third click should be interpreted as first click again, fourth click should be double click etc.
        currentInput->tapCount = (currentInput->tapCount % 2) ? 1 : 2;
    }

    eDragState newState = NoDrag;
    for (const auto& orderAndSystem : systems)
    {
        newState = Max(newState, orderAndSystem.second->RequireNewState(currentInput));
    }
    SetDragState(newState);

    for (auto it = systems.rbegin(); it != systems.rend(); ++it)
    {
        BaseEditorSystem* system = (*it).second;
        if (system->CanProcessInput(currentInput, false))
        {
            system->ProcessInput(currentInput, false);
        }
    }
}

void EditorSystemsManager::EmulateInput(DAVA::UIEvent* generatedEvent)
{
    if (generatedEvent->device == eInputDevices::MOUSE)
    {
        mouseDelta = generatedEvent->point - lastMousePos;
    }

    for (auto it = systems.rbegin(); it != systems.rend(); ++it)
    {
        BaseEditorSystem* system = (*it).second;
        if (system->CanProcessInput(generatedEvent, true))
        {
            system->ProcessInput(generatedEvent, true);
        }
    }
}

void EditorSystemsManager::OnEmulationModeChanged(const DAVA::Any& emulationModeValue)
{
    bool emulationMode = emulationModeValue.Cast<bool>(false);
    SetDisplayState(emulationMode ? Emulation : previousDisplayState);
}

ControlNode* EditorSystemsManager::GetControlNodeAtPoint(const DAVA::Vector2& point, bool canGoDeeper) const
{
    if (accessor->GetActiveContext() == nullptr)
    {
        return nullptr;
    }

    if (!DAVA::TArc::IsKeyPressed(eModifierKeys::CONTROL))
    {
        return selectionSystemPtr->GetCommonNodeUnderPoint(point, canGoDeeper);
    }
    return selectionSystemPtr->GetNearestNodeUnderPoint(point);
}

uint32 EditorSystemsManager::GetIndexOfNearestRootControl(const DAVA::Vector2& point) const
{
    using namespace DAVA::TArc;

    SortedControlNodeSet displayedRootControls = GetDisplayedRootControls();
    if (displayedRootControls.empty())
    {
        return 0;
    }

    DataContext* globalContext = accessor->GetGlobalContext();
    CanvasModuleData* canvasModuleData = globalContext->GetData<CanvasModuleData>();
    DVASSERT(canvasModuleData != nullptr);
    uint32 index = canvasModuleData->GetEditorControlsView()->GetIndexByPos(point);
    bool insertToEnd = (index == displayedRootControls.size());

    auto iter = displayedRootControls.begin();
    std::advance(iter, insertToEnd ? index - 1 : index);
    PackageBaseNode* target = *iter;

    DataContext* activeContext = accessor->GetActiveContext();
    DVASSERT(activeContext != nullptr);
    DocumentData* documentData = activeContext->GetData<DocumentData>();
    PackageNode* package = documentData->GetPackageNode();

    PackageControlsNode* controlsNode = package->GetPackageControlsNode();
    for (uint32 i = 0, count = controlsNode->GetCount(); i < count; ++i)
    {
        if (controlsNode->Get(i) == target)
        {
            return insertToEnd ? i + 1 : i;
        }
    }

    return controlsNode->GetCount();
}

void EditorSystemsManager::SelectAll()
{
    selectionSystemPtr->SelectAllControls();
}

void EditorSystemsManager::FocusNextChild()
{
    selectionSystemPtr->FocusNextChild();
}

void EditorSystemsManager::FocusPreviousChild()
{
    selectionSystemPtr->FocusPreviousChild();
}

void EditorSystemsManager::ClearSelection()
{
    selectionSystemPtr->ClearSelection();
}

void EditorSystemsManager::SelectNode(ControlNode* node)
{
    selectionSystemPtr->SelectNode(node);
}

void EditorSystemsManager::SetDisplayState(eDisplayState newDisplayState)
{
    if (displayState == newDisplayState)
    {
        return;
    }

    previousDisplayState = displayState;
    displayState = newDisplayState;
    for (const auto& orderAndSystem : systems)
    {
        orderAndSystem.second->OnDisplayStateChanged(displayState, previousDisplayState);
    }
}

void EditorSystemsManager::OnRootContolsChanged(const DAVA::Any& rootControlsValue)
{
    const EngineContext* engineContext = GetEngineContext();
    // reset current screen to apply correct modal control
    engineContext->uiControlSystem->GetInputSystem()->SetCurrentScreen(engineContext->uiControlSystem->GetScreen());
}

void EditorSystemsManager::UpdateDisplayState()
{
    using namespace DAVA::TArc;

    SortedControlNodeSet rootControls;

    DataContext* activeContext = accessor->GetActiveContext();
    if (activeContext != nullptr)
    {
        DocumentData* documentData = activeContext->GetData<DocumentData>();
        rootControls = documentData->GetDisplayedRootControls();
    }

    eDisplayState state = rootControls.size() == 1 ? Edit : Preview;
    if (displayState == Emulation)
    {
        previousDisplayState = state;
    }
    else
    {
        SetDisplayState(state);
    }
}

void EditorSystemsManager::OnActiveHUDAreaChanged(const HUDAreaInfo& areaInfo)
{
    currentHUDArea = areaInfo;
}

void EditorSystemsManager::OnUpdate()
{
    using namespace DAVA;

    UpdateDisplayState();

    for (const auto& orderAndSystem : systems)
    {
        orderAndSystem.second->OnUpdate();
    }
}

void EditorSystemsManager::InitDAVAScreen()
{
    RefPtr<UIControl> backgroundControl(new UIControl());

    backgroundControl->SetName(FastName("Background_control_of_scroll_area_controller"));
    ScopedPtr<UIScreen> davaUIScreen(new UIScreen());
    UIControlBackground* screenBackground = davaUIScreen->GetOrCreateComponent<UIControlBackground>();
    screenBackground->SetDrawType(UIControlBackground::DRAW_FILL);
    screenBackground->SetColor(Color(0.3f, 0.3f, 0.3f, 1.0f));
    const EngineContext* engineContext = GetEngineContext();
    engineContext->uiScreenManager->RegisterScreen(0, davaUIScreen);
    engineContext->uiScreenManager->SetFirst(0);

    engineContext->uiScreenManager->GetScreen()->AddControl(backgroundControl.Get());
    backgroundControl->AddControl(rootControl.Get());
}

void EditorSystemsManager::OnDragStateChanged(eDragState currentState, eDragState previousState)
{
    using namespace DAVA::TArc;

    if (currentState == Transform || previousState == Transform)
    {
        DataContext* activeContext = accessor->GetActiveContext();
        DVASSERT(activeContext != nullptr);
        DocumentData* documentData = activeContext->GetData<DocumentData>();
        PackageNode* package = documentData->GetPackageNode();
        //calling this function can refresh all properties and styles in this node
        package->SetCanUpdateAll(previousState == Transform);
    }
}

Vector2 EditorSystemsManager::GetMouseDelta() const
{
    return mouseDelta;
}

DAVA::Vector2 EditorSystemsManager::GetLastMousePos() const
{
    return lastMousePos;
}

void EditorSystemsManager::OnPackageChanged(const DAVA::Any& /*packageValue*/)
{
    magnetLinesChanged.Emit({});
    SetDragState(NoDrag);
}

EditorSystemsManager::eDragState EditorSystemsManager::GetDragState() const
{
    return dragState;
}

EditorSystemsManager::eDisplayState EditorSystemsManager::GetDisplayState() const
{
    return displayState;
}

HUDAreaInfo EditorSystemsManager::GetCurrentHUDArea() const
{
    return currentHUDArea;
}

void EditorSystemsManager::SetDragState(eDragState newDragState)
{
    if (dragState == newDragState)
    {
        return;
    }
    previousDragState = dragState;
    dragState = newDragState;
    OnDragStateChanged(dragState, previousDragState);
    for (const auto& orderAndSystem : systems)
    {
        orderAndSystem.second->OnDragStateChanged(dragState, previousDragState);
    }
}

const SortedControlNodeSet& EditorSystemsManager::GetDisplayedRootControls() const
{
    using namespace DAVA::TArc;
    DataContext* activeContext = accessor->GetActiveContext();
    if (activeContext == nullptr)
    {
        static SortedControlNodeSet empty;
        return empty;
    }

    DocumentData* documentData = activeContext->GetData<DocumentData>();
    return documentData->GetDisplayedRootControls();
}

void EditorSystemsManager::RegisterEditorSystem(BaseEditorSystem* editorSystem)
{
    BaseEditorSystem::eSystems order = editorSystem->GetOrder();

    CanvasControls newControls = editorSystem->CreateCanvasControls();
    if (newControls.empty() == false)
    {
        auto greatestOrderAndSystem = systems.lower_bound(order);
        while (greatestOrderAndSystem != systems.end() && systemsControls.find(greatestOrderAndSystem->second) == systemsControls.end())
        {
            greatestOrderAndSystem++;
        }

        if (greatestOrderAndSystem == systems.end() || systemsControls.find(greatestOrderAndSystem->second) == systemsControls.end())
        {
            for (auto& control : newControls)
            {
                rootControl->AddControl(control.Get());
            }
        }
        else
        {
            BaseEditorSystem* foundSystem = greatestOrderAndSystem->second;
            const DAVA::Vector<DAVA::RefPtr<DAVA::UIControl>>& foundCountrols = systemsControls.find(foundSystem)->second;
            DAVA::UIControl* firstFoundControl = foundCountrols.begin()->Get();
            for (auto& control : newControls)
            {
                rootControl->InsertChildBelow(control.Get(), firstFoundControl);
            }
        }

        systemsControls[editorSystem] = newControls;
    }

    systems[order] = editorSystem;
}

void EditorSystemsManager::UnregisterEditorSystem(BaseEditorSystem* editorSystem)
{
    auto iter = systemsControls.find(editorSystem);
    if (iter != systemsControls.end())
    {
        const CanvasControls& canvasControls = systemsControls[editorSystem];

        for (const auto& control : canvasControls)
        {
            rootControl->RemoveControl(control.Get());
        }
        editorSystem->DeleteCanvasControls(canvasControls);
    }

    systems.erase(editorSystem->GetOrder());
    systemsControls.erase(editorSystem);
}
