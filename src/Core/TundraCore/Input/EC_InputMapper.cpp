/**
    For conditions of distribution and use, see copyright notice in LICENSE

    @file   EC_InputMapper.cpp
    @brief  EC_InputMapper translates given set of key sequences to Entity Actions on the entity the component is part of. */

#include "StableHeaders.h"
#include "DebugOperatorNew.h"

#include "EC_InputMapper.h"
#include "InputAPI.h"

#include "Framework.h"
#include "IAttribute.h"
#include "AttributeMetadata.h"
#include "Entity.h"
#include "LoggingFunctions.h"

#include "MemoryLeakCheck.h"

EC_InputMapper::EC_InputMapper(Scene* scene):
    IComponent(scene),
    INIT_ATTRIBUTE_VALUE(contextName, "Input context name", "EC_InputMapper"),
    INIT_ATTRIBUTE_VALUE(contextPriority, "Input context priority", 90),
    INIT_ATTRIBUTE_VALUE(takeKeyboardEventsOverQt, "Take keyboard events over Qt", false),
    INIT_ATTRIBUTE_VALUE(takeMouseEventsOverQt, "Take mouse events over Qt", false),
    INIT_ATTRIBUTE_VALUE(executionType, "Action execution type", 1),
    INIT_ATTRIBUTE_VALUE(modifiersEnabled, "Key modifiers enable", true),
    INIT_ATTRIBUTE_VALUE(enabled, "Enable actions", true),
    INIT_ATTRIBUTE_VALUE(keyrepeatTrigger, "Trigger on keyrepeats", true),
    INIT_ATTRIBUTE_VALUE(suppressKeyEvents, "Suppress used keyboard events", false),
    INIT_ATTRIBUTE_VALUE(suppressMouseEvents, "Suppress used mouse events", false)
{
    LogWarning("EC_InputMapper is deprecated and should not be used. Use InputContext instead.");
    static AttributeMetadata executionAttrData;
    static bool metadataInitialized = false;
    if(!metadataInitialized)
    {
        executionAttrData.enums[EntityAction::Local] = "Local";
        executionAttrData.enums[EntityAction::Server] = "Server";
        executionAttrData.enums[EntityAction::Server | EntityAction::Local] = "Local+Server";
        executionAttrData.enums[EntityAction::Peers] = "Peers";
        executionAttrData.enums[EntityAction::Peers | EntityAction::Local] = "Local+Peers";
        executionAttrData.enums[EntityAction::Peers | EntityAction::Server] = "Local+Server";
        executionAttrData.enums[EntityAction::Peers | EntityAction::Server | EntityAction::Local] = "Local+Server+Peers";
        metadataInitialized = true;
    }
    executionType.SetMetadata(&executionAttrData);

    connect(this, SIGNAL(ParentEntitySet()), SLOT(Initialize()));
}

EC_InputMapper::~EC_InputMapper()
{
    inputContext.reset();
}

void EC_InputMapper::RegisterMapping(const QKeySequence &keySeq, const QString &action, int eventType, int executionType)
{
    ActionInvocation invocation;
    invocation.name = action;
    invocation.executionType = executionType;
    actionInvokationMappings[qMakePair(keySeq, (KeyEvent::EventType)eventType)] = invocation;
}

void EC_InputMapper::RegisterMapping(const QString &keySeq, const QString &action, int eventType, int executionType)
{
    ActionInvocation invocation;
    invocation.name = action;
    invocation.executionType = executionType;
    QKeySequence key(keySeq);
    if (!key.isEmpty())
        actionInvokationMappings[qMakePair(key, (KeyEvent::EventType)eventType)] = invocation;
}

void EC_InputMapper::RemoveMapping(const QKeySequence &keySeq, int eventType)
{
    ActionInvocationMap::iterator it = actionInvokationMappings.find(qMakePair(keySeq, (KeyEvent::EventType)eventType));
    if (it != actionInvokationMappings.end())
        actionInvokationMappings.erase(it);
}

void EC_InputMapper::RemoveMapping(const QString &keySeq, int eventType)
{
    ActionInvocationMap::iterator it = actionInvokationMappings.find(qMakePair(QKeySequence(keySeq), (KeyEvent::EventType)eventType));
    if (it != actionInvokationMappings.end())
        actionInvokationMappings.erase(it);
}

void EC_InputMapper::AttributesChanged()
{
    if (contextName.ValueChanged())
        inputContext->SetName(contextName.Get());
    if (contextPriority.ValueChanged())
        inputContext->SetPriority(contextPriority.Get());
    if(takeKeyboardEventsOverQt.ValueChanged())
        inputContext->SetTakeKeyboardEventsOverQt(takeKeyboardEventsOverQt.Get());
    if(takeMouseEventsOverQt.ValueChanged())
        inputContext->SetTakeMouseEventsOverQt(takeMouseEventsOverQt.Get());
}

void EC_InputMapper::Initialize()
{
    inputContext = GetFramework()->Input()->RegisterInputContext(contextName.Get().toStdString().c_str(), contextPriority.Get());
    inputContext->SetTakeKeyboardEventsOverQt(takeKeyboardEventsOverQt.Get());
    inputContext->SetTakeMouseEventsOverQt(takeMouseEventsOverQt.Get());
    connect(inputContext.get(), SIGNAL(KeyEventReceived(KeyEvent *)), SLOT(HandleKeyEvent(KeyEvent *)));
    connect(inputContext.get(), SIGNAL(MouseEventReceived(MouseEvent *)), SLOT(HandleMouseEvent(MouseEvent *)));
}

void EC_InputMapper::HandleKeyEvent(KeyEvent *e)
{
    if (!enabled.Get())
        return;
    if (!keyrepeatTrigger.Get() && e->eventType == KeyEvent::KeyPressed && e->keyPressCount > 1)
        return; // ignore key repeats as requested

    ActionInvocationMap::const_iterator it;
    // If 'modifiers are enabled', then it means we distinguish 'E', 'Shift+E' and 'Ctrl+E' as separate key sequences...
    // But this separation can only be done for pressed and hold-down keyboard events. If 'E' is released, it needs to be detected as released individual of
    // whether any modifiers are down.
    if (modifiersEnabled.Get() && e->eventType != KeyEvent::KeyReleased) 
        it = actionInvokationMappings.find(qMakePair(QKeySequence(e->keyCode | e->modifiers), e->eventType));
    else // .. otherwise, we treat 'E', 'Shift+E' and 'Ctrl+E' all just simply as 'E'.
        it = actionInvokationMappings.find(qMakePair(QKeySequence(e->keyCode), e->eventType));
    if (it == actionInvokationMappings.end())
        return;

    Entity *entity = ParentEntity();
    if (!entity)
    {
        LogWarning("Parent entity not set. Cannot execute action.");
        return;
    }

    const ActionInvocation& invocation = it.value();
    const QString &action = invocation.name;
    int execType = invocation.executionType;
    // If zero execution type, use default
    if (!execType)
        execType = executionType.Get();

    // If the action has parameters, parse them from the action string.
    QString command;
    QStringList parameters;
    ParseCommand(action, command, parameters);
    entity->Exec((EntityAction::ExecTypeField)execType, command, parameters);

    if (suppressKeyEvents.Get())
        e->Suppress();
}

void EC_InputMapper::HandleMouseEvent(MouseEvent *e)
{
    if (!enabled.Get())
        return;
    if (!ParentEntity())
        return;
    
    /// \todo this hard coding of look button logic is not nice! Android implements left mouse move (touch drag) as mouselook currently.
#ifdef ANDROID
    if (e->IsButtonDown(MouseEvent::LeftButton))
#else
    if ((e->IsButtonDown(MouseEvent::RightButton)) && (!GetFramework()->Input()->IsMouseCursorVisible()))
#endif
    {
        if (e->relativeX != 0)
        {
            ParentEntity()->Exec((EntityAction::ExecTypeField)executionType.Get(), "MouseLookX" , QString::number(e->relativeX));
            if (suppressMouseEvents.Get())
                e->Suppress();
        }
        if (e->relativeY != 0)
        {
            ParentEntity()->Exec((EntityAction::ExecTypeField)executionType.Get(), "MouseLookY" , QString::number(e->relativeY));
            if (suppressMouseEvents.Get())
                e->Suppress();
        }
    }
    if (e->eventType == MouseEvent::MouseScroll && e->relativeZ != 0)
    {
        ParentEntity()->Exec((EntityAction::ExecTypeField)executionType.Get(), "MouseScroll" , QString::number(e->relativeZ));
        if (suppressMouseEvents.Get())
            e->Suppress();
    }
}
