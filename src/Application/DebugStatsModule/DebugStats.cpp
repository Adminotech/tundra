/**
 *  For conditions of distribution and use, see copyright notice in LICENSE
 *
 *  @file   DebugStats.cpp
 *  @brief  Shows information about internal core data structures in separate windows.
 */

#include "StableHeaders.h"
#include "DebugOperatorNew.h"

#include "DebugStats.h"
#include "TimeProfilerWindow.h"

#include "Framework.h"
#include "UiAPI.h"
#include "LoggingFunctions.h"
#include "SceneAPI.h"
#include "Scene/Scene.h"
#include "Entity.h"
#include "Renderer.h"
#include "UiProxyWidget.h"
#include "ConsoleAPI.h"
#include "InputAPI.h"
#include "UiAPI.h"
#include "UiMainWindow.h"
#include "Profiler.h"

#include <utility>
#include <QDebug>

#include <QCryptographicHash>

#include "StaticPluginRegistry.h"

#include "MemoryLeakCheck.h"

using namespace std;

void DumpProfilerToLog(ProfilerNodeTree* node, int indent, int elapsedFrames);

DebugStatsModule::DebugStatsModule() :
    IModule("DebugStats"),
    profilerWindow_(0),
    profilerLogDumpElapsedFrames(0),
    enableProfilerLogDump(false)
{
}

DebugStatsModule::~DebugStatsModule()
{
    SAFE_DELETE(profilerWindow_);
}

void DebugStatsModule::Initialize()
{
    lastCallTime = GetCurrentClockTime();
    lastProfilerDumpTime = GetCurrentClockTime();

    enableProfilerLogDump = framework_->HasCommandLineParameter("--dumpProfiler");

    // If not needed by anything but profiler window, disable spinning blocks when not visible.
    if (!enableProfilerLogDump)
        framework_->GetProfiler()->SetEnabled(false);

    framework_->Console()->RegisterCommand("prof", "Shows the profiling window.",
        this, SLOT(ShowProfilerWindow()));
    framework_->Console()->RegisterCommand("exec", "Invokes an Entity Action on an entity (debugging).",
        this, SLOT(Exec(const QStringList &)));

    inputContext = framework_->Input()->RegisterInputContext("DebugStatsInput", 90);
    connect(inputContext.get(), SIGNAL(KeyPressed(KeyEvent *)), this, SLOT(HandleKeyPressed(KeyEvent *)));
}

void DebugStatsModule::HandleKeyPressed(KeyEvent *e)
{
    if (e->eventType != KeyEvent::KeyPressed || e->keyPressCount > 1)
        return;

    const QKeySequence showProfiler = framework_->Input()->KeyBinding("ShowProfilerWindow", QKeySequence(Qt::ShiftModifier + Qt::Key_P));
    if (QKeySequence(e->keyCode | e->modifiers) == showProfiler)
        ShowProfilerWindow();
}

void DebugStatsModule::StartProfiling(bool visible)
{
    profilerWindow_->SetVisibility(visible);

    // If not needed by anything but profiler window, disable spinning blocks when not visible.
    framework_->GetProfiler()->SetEnabled(visible || enableProfilerLogDump);

    if (visible)
        profilerWindow_->Refresh(); 
}

void DebugStatsModule::ShowProfilerWindow()
{
    framework_->GetProfiler()->SetEnabled(true);

    // If the window is already created toggle its visibility. If visible, bring it to front.
    if (profilerWindow_)
    {
        profilerWindow_->setVisible(!(profilerWindow_->isVisible()));
        if (profilerWindow_->isVisible())
            framework_->Ui()->BringWidgetToFront(profilerWindow_);
        return;
    }

    profilerWindow_ = new TimeProfilerWindow(framework_, framework_->Ui()->MainWindow());
    profilerWindow_->setWindowFlags(Qt::Tool);
    profilerWindow_->resize(1050, 530);
    connect(profilerWindow_, SIGNAL(Visible(bool)), SLOT(StartProfiling(bool)));
    profilerWindow_->show();
}

TimeProfilerWindow *DebugStatsModule::ProfilerWindow() const
{
    return profilerWindow_.data();
}

void DebugStatsModule::Update(f64 frametime)
{
    tick_t now = GetCurrentClockTime();
    double timeSpent = ProfilerBlock::ElapsedTimeSeconds(lastCallTime, now);
    lastCallTime = now;

#ifdef PROFILING
    if (enableProfilerLogDump)
    {
	    ++profilerLogDumpElapsedFrames;        
        if (ProfilerBlock::ElapsedTimeSeconds(lastProfilerDumpTime, now) > 5.0)
        { 
            LogInfo("Dumping profiling data...");
            lastProfilerDumpTime = now;
            Profiler &profiler = *framework_->GetProfiler();
            profiler.Lock();
            DumpProfilerToLog(profiler.GetRoot(), 0, profilerLogDumpElapsedFrames);
            profiler.Release();
	        profilerLogDumpElapsedFrames = 0;
        }        
    }
#endif

    if (profilerWindow_ && profilerWindow_->isVisible())
    {
        frameTimes.push_back(make_pair(*(u64*)&now, timeSpent));
        if (frameTimes.size() > 2048) // Maintain an upper bound in the frame history.
            frameTimes.erase(frameTimes.begin());

        profilerWindow_->RedrawFrameTimeHistoryGraph(frameTimes);
        /// @todo Should this be enabled back?
        //profilerWindow_->DoThresholdLogging();
    }
}

void DebugStatsModule::Exec(const QStringList &params)
{
    if (params.size() < 2)
    {
        LogError("Not enough parameters.");
        return;
    }

    bool ok;
    int id = params[0].toInt(&ok);
    if (!ok)
    {
        LogError("Invalid value for entity ID. The ID must be an integer and unequal to zero.");
        return;
    }

    Scene *scene = GetFramework()->Scene()->MainCameraScene();
    if (!scene)
    {
        LogError("No active scene.");
        return;
    }

    EntityPtr entity = scene->GetEntity(id);
    if (!entity)
    {
        LogError("No entity found for entity ID " + params[0]);
        return;
    }

    QStringList execParameters;
    if (params.size() >= 3)
    {
        int type = params[2].toInt(&ok);
        if (!ok)
        {
            LogError("Invalid execution type: must be 0-7");
            return;
        }

        for(int i = 3; i < params.size(); ++i)
            execParameters << params[i];

        entity->Exec((EntityAction::ExecTypeField)type, params[1], execParameters);
    }
    else
        entity->Exec(EntityAction::Local, params[1], execParameters);
}

void DumpProfilerToLog(ProfilerNodeTree* node, int indent, int elapsedFrames)
{
    if (!node)
	return;

    const ProfilerNode *timings_node = dynamic_cast<const ProfilerNode*>(node);

    if (timings_node && timings_node->num_called_custom_)
    {
        char str[256];
        char tempStr[256];
        if (indent > 0)
            memset(str, ' ', indent);

        sprintf(tempStr, "%s: Calls %d Calls/frame %.2f Total %.2fms Frame %.2fms", timings_node->Name().c_str(), (int)timings_node->num_called_custom_, (float)timings_node->num_called_custom_ / elapsedFrames, timings_node->total_custom_*1000.f, timings_node->total_custom_*1000.f / elapsedFrames);
        strcpy(&str[indent], tempStr);
    
        LogInfo(QString(str));

        timings_node->num_called_custom_ = 0;
        timings_node->total_custom_ = 0;
        timings_node->custom_elapsed_min_ = 1e9;
        timings_node->custom_elapsed_max_ = 0;
    }

    const ProfilerNodeTree::NodeList& children = node->GetChildren();
    for (ProfilerNodeTree::NodeList::const_iterator i = children.begin(); i != children.end(); ++i)
        DumpProfilerToLog(i->get(), indent + 1, elapsedFrames);
}

extern "C"
{
#ifndef ANDROID
DLLEXPORT void TundraPluginMain(Framework *fw)
#else
DEFINE_STATIC_PLUGIN_MAIN(DebugStatsModule)
#endif
{
    Framework::SetInstance(fw); // Inside this DLL, remember the pointer to the global framework object.
    IModule *module = new DebugStatsModule();
    fw->RegisterModule(module);
}
}
