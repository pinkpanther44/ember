#include "CrashLog.h"
#include "AppSettings.h"
#include "Branding.h"   // 8.316：**名乗る名前はブランドごと**（Phase 309）

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace CrashLog
{

juce::File getReportFolder()
{
    return AppSettings::getDataFolder().getChildFile ("crash");
}

int getNumStoredReports()
{
    auto folder = getReportFolder();

    if (! folder.isDirectory())
        return 0;

    return folder.getNumberOfChildFiles (juce::File::findFiles, "crash-*.log");
}

#if JUCE_WINDOWS

namespace
{
    /** 番地を「モジュール名＋その中でのRVA」にする。

        **RVAで出すこと。** 生の番地はASLRで毎回変わるので、
        次に落ちたときの番地と見比べても意味がありません。
        RVAなら`.map`とも、イベントログの「フォールト オフセット」とも突き合わせられます。 */
    juce::String describeAddress (DWORD64 address)
    {
        HMODULE module = nullptr;

        if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                    | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  (LPCWSTR) (ULONG_PTR) address, &module)
             || module == nullptr)
        {
            return juce::String::toHexString ((juce::int64) address) + "  (module unknown)";
        }

        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW (module, path, MAX_PATH);

        const auto name = juce::File (juce::String (path)).getFileName();
        const auto rva = (juce::int64) (address - (DWORD64) (ULONG_PTR) module);

        return name + " + 0x" + juce::String::toHexString (rva);
    }

    /** `ExceptionInfo`の文脈から呼び出しの道筋を辿る。

        **`dbghelp`は使いません**（`StackWalk64`）。x64の巻き戻し表はntdllが持っていて、
        `RtlLookupFunctionEntry`と`RtlVirtualUnwind`だけで辿れます
        ——**シンボルが無くても正確**で、配るものに何も足しません。 */
    juce::String walkStack (CONTEXT context)
    {
        juce::String text;

        for (int frame = 0; frame < 64; ++frame)
        {
            if (context.Rip == 0)
                break;

            text << "  #" << juce::String (frame).paddedLeft ('0', 2) << "  "
                 << describeAddress (context.Rip) << juce::newLine;

            DWORD64 imageBase = 0;
            auto* function = RtlLookupFunctionEntry (context.Rip, &imageBase, nullptr);

            if (function == nullptr)
            {
                // 葉の関数（巻き戻し表を持たないもの）。戻り先はスタックの先頭にある
                if (context.Rsp == 0)
                    break;

                context.Rip = *(DWORD64*) context.Rsp;
                context.Rsp += 8;
                continue;
            }

            PVOID handlerData = nullptr;
            DWORD64 establisherFrame = 0;

            RtlVirtualUnwind (UNW_FLAG_NHANDLER, imageBase, context.Rip, function,
                              &context, &handlerData, &establisherFrame, nullptr);
        }

        return text;
    }

    LONG WINAPI handler (EXCEPTION_POINTERS* info)
    {
        if (info == nullptr || info->ExceptionRecord == nullptr || info->ContextRecord == nullptr)
            return EXCEPTION_CONTINUE_SEARCH;

        auto folder = getReportFolder();
        folder.createDirectory();

        const auto now = juce::Time::getCurrentTime();
        auto file = folder.getChildFile ("crash-" + now.formatted ("%Y%m%d-%H%M%S") + ".log");

        juce::String text;
        text << Branding::productName << " " << JUCE_APPLICATION_VERSION_STRING
             << "  " << now.toISO8601 (true) << juce::newLine
             << "exception 0x" << juce::String::toHexString ((juce::int64) info->ExceptionRecord->ExceptionCode)
             << "  at " << describeAddress ((DWORD64) (ULONG_PTR) info->ExceptionRecord->ExceptionAddress)
             << juce::newLine;

        // アクセス違反なら「読もうとした番地」も出す。0なら null を踏んでいる
        if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
             && info->ExceptionRecord->NumberParameters >= 2)
        {
            text << "  " << (info->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading" : "writing")
                 << " 0x" << juce::String::toHexString ((juce::int64) info->ExceptionRecord->ExceptionInformation[1])
                 << juce::newLine;
        }

        text << juce::newLine
             << "thread " << (int) GetCurrentThreadId() << juce::newLine
             << walkStack (*info->ContextRecord);

        file.replaceWithText (text);

        return EXCEPTION_EXECUTE_HANDLER;   // 書けたので、そのまま終わる
    }
}

void install()
{
    SetUnhandledExceptionFilter (handler);
}

bool runSelfTestIfRequested (const juce::String& commandLine)
{
    if (! commandLine.contains ("--crash-selftest"))
        return false;

    std::cout << "crash log folder : " << getReportFolder().getFullPathName() << std::endl;
    std::cout << "about to dereference a null pointer on purpose..." << std::endl;

    // **わざと踏みます。** `volatile`にしてあるのは、最適化で消されないようにするため
    volatile int* nothing = nullptr;
    *nothing = 1;

    return true;   // ここへは来ない
}

#else

void install() {}
bool runSelfTestIfRequested (const juce::String&) { return false; }

#endif

} // namespace CrashLog
