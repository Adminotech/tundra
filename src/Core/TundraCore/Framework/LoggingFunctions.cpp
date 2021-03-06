/**
    For conditions of distribution and use, see copyright notice in LICENSE

    @file   LoggingFunctions.cpp
    @brief  Tundra logging utility functions. */

#include "StableHeaders.h"

#include "LoggingFunctions.h"
#include "Framework.h"
#include "ConsoleAPI.h"
#include "Application.h"
#include "Win.h"

#ifdef ANDROID
#include <android/log.h>
#endif

void PrintLogMessage(u32 logChannel, const QString &str)
{
    if (!IsLogChannelEnabled(logChannel))
        return;

    Framework *instance = Framework::Instance();
    ConsoleAPI *console = (instance ? instance->Console() : 0);

    // On Windows, highlight errors and warnings.
#ifdef WIN32
    HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdoutHandle == INVALID_HANDLE_VALUE)
        return;
    if ((logChannel & LogChannelError) != 0) SetConsoleTextAttribute(stdoutHandle, FOREGROUND_RED | FOREGROUND_INTENSITY);
    else if ((logChannel & LogChannelWarning) != 0) SetConsoleTextAttribute(stdoutHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
#endif
    // The console and stdout prints are equivalent.
    if (console)
        console->Print(str);
    else // The Console API is already dead for some reason, print directly to stdout to guarantee we don't lose any logging messages.
        PrintRaw(str);

    // Restore the text color to normal.
#ifdef WIN32
    SetConsoleTextAttribute(stdoutHandle, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#endif
}

bool IsLogChannelEnabled(u32 logChannel)
{
    Framework *instance = Framework::Instance();
    ConsoleAPI *console = (instance ? instance->Console() : 0);
    if (console)
        return console->IsLogChannelEnabled(logChannel);
    else
        return true; // We've already killed Framework and Console! Print out everything so that we can't accidentally lose any important messages.
}

void PrintRaw(const QString &str)
{
#if defined(WIN32)
    HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdoutHandle == INVALID_HANDLE_VALUE)
        return;
    const std::wstring wstr = QStringToWString(str);
    DWORD charsWritten;
    WriteConsoleW(stdoutHandle, wstr.c_str(), static_cast<DWORD>(wstr.length()), &charsWritten, 0);
#elif defined(ANDROID)
    __android_log_print(ANDROID_LOG_INFO, Application::ApplicationName(), "%s", str.toStdString().c_str());
#else
    printf("%s", str.toStdString().c_str());
#endif
}
