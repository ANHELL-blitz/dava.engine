#include "LayoutIssuesHandler.h"

#include "Classes/Model/PackageHierarchy/PackageControlsNode.h"
#include "Classes/Model/PackageHierarchy/PackageNode.h"
#include "Classes/Modules/DocumentsModule/DocumentData.h"
#include "Classes/Modules/IssueNavigatorModule/IssueHelper.h"
#include "Classes/Modules/IssueNavigatorModule/IssueNavigatorWidget.h"

#include <Engine/Engine.h>
#include <UI/Layouts/LayoutFormula.h>
#include <UI/Layouts/UILayoutSystem.h>
#include <UI/UIControl.h>
#include <UI/UIControlSystem.h>

#include <TArc/Core/ContextAccessor.h>

using namespace DAVA;

LayoutIssuesHandler::LayoutIssuesHandler(DAVA::TArc::ContextAccessor* accessor_, DAVA::int32 sectionId_, IssueNavigatorWidget* widget_, IndexGenerator& indexGenerator_)
    : sectionId(sectionId_)
    , widget(widget_)
    , accessor(accessor_)
    , indexGenerator(indexGenerator_)
{
    GetEngineContext()->uiControlSystem->GetLayoutSystem()->AddListener(this);
}

LayoutIssuesHandler::~LayoutIssuesHandler()
{
    GetEngineContext()->uiControlSystem->GetLayoutSystem()->RemoveListener(this);
}

void LayoutIssuesHandler::OnFormulaProcessed(UIControl* control, Vector2::eAxis axis, const LayoutFormula* formula)
{
    if (formula->HasError())
    {
        auto it = createdIssues[axis].find(control);
        if (it != createdIssues[axis].end())
        {
            widget->ChangeMessage(sectionId, it->second, formula->GetErrorMessage());
        }
        else
        {
            const DocumentData* data = accessor->GetActiveContext()->GetData<DocumentData>();
            DVASSERT(data != nullptr);

            String pathToControl = control->GetName().c_str(); // UIControlHelpers::GetControlPath() should be used after DF-14277 implementing

            auto GetParentControl = [&](const UIControl* control) -> UIControl*
            {
                return IsRootControl(control) ? nullptr : control->GetParent();
            };

            for (UIControl* parentControl = GetParentControl(control);
                 parentControl != nullptr;
                 parentControl = GetParentControl(parentControl))
            {
                String name = "";
                if (parentControl->GetName().IsValid())
                {
                    name = parentControl->GetName().c_str();
                }
                pathToControl = name + "/" + pathToControl;
            }

            Issue issue;
            issue.sectionId = sectionId;
            issue.issueId = indexGenerator.NextIssueId();
            issue.message = formula->GetErrorMessage();
            issue.packagePath = data->GetPackagePath().GetFrameworkPath();
            issue.pathToControl = pathToControl;
            issue.propertyName = axis == Vector2::AXIS_X ? "SizePolicy/horizontalFormula" : "SizePolicy/verticalFormula";

            widget->AddIssue(issue);

            createdIssues[axis][control] = issue.issueId;
        }
    }
    else
    {
        RemoveIssue(control, axis);
    }
}

void LayoutIssuesHandler::OnFormulaRemoved(UIControl* control, Vector2::eAxis axis, const LayoutFormula* formula)
{
    RemoveIssue(control, axis);
}

void LayoutIssuesHandler::RemoveIssue(DAVA::UIControl* control, DAVA::Vector2::eAxis axis)
{
    auto it = createdIssues[axis].find(control);
    if (it != createdIssues[axis].end())
    {
        widget->RemoveIssue(sectionId, it->second);
        createdIssues[axis].erase(it);
    }
}

bool LayoutIssuesHandler::IsRootControl(const UIControl* control) const
{
    const DocumentData* data = accessor->GetActiveContext()->GetData<DocumentData>();
    DVASSERT(data != nullptr);

    PackageControlsNode* controls = data->GetPackageNode()->GetPackageControlsNode();
    for (ControlNode* controlNode : *controls)
    {
        if (controlNode->GetControl() == control)
        {
            return true;
        }
    }

    PackageControlsNode* prototypes = data->GetPackageNode()->GetPrototypes();
    for (ControlNode* controlNode : *prototypes)
    {
        if (controlNode->GetControl() == control)
        {
            return true;
        }
    }
    return false;
}
