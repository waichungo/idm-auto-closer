#include <Windows.h>
#include <string>
#include <iostream>
#include <vector>
#include <sstream>

using std::string;
using std::vector;
typedef BOOL(__stdcall *ProcQueryFullProcessImageNameA)(HANDLE hProcess, DWORD dwFlags, LPSTR lpExeName, PDWORD lpdwSize);
ProcQueryFullProcessImageNameA QueryFullProcessImageNameAFunc = (ProcQueryFullProcessImageNameA)GetProcAddress(GetModuleHandle("Kernel32.dll"), "QueryFullProcessImageNameA");
class WindowObject
{
public:
    std::string title;
    std::string filePath;
    std::string className;
    int id;
    HWND window;
};
BOOL WindowProc(HWND win, LPARAM param)
{
    vector<WindowObject> *windows = (vector<WindowObject> *)param;
    WindowObject obj{};
    DWORD size = 1024;
    char className[1024] = {0};
    char title[1024] = {0};
    char path[1024] = {0};
    DWORD id = 0;

    GetClassNameA(win, className, sizeof(className));
    GetWindowTextA(win, title, sizeof(title));
    if (GetWindowThreadProcessId(win, &id) > 0)
    {
        if (QueryFullProcessImageNameAFunc)
        {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
                                              PROCESS_VM_READ,
                                          FALSE, id);

            // Get the process name.

            if (NULL != hProcess)
            {
                QueryFullProcessImageNameAFunc(hProcess, 0, path, &size);
                CloseHandle(hProcess);
            }
        }
    }

    obj.className = className;
    obj.title = title;
    obj.filePath = path;
    obj.id = (int)id;
    obj.window = win;

    windows->push_back(obj);

    return TRUE;
}
typedef struct ExecResult
{
    int exitcode;
    std::string result;
} ExecResult;
static DWORD minimum(DWORD one, DWORD two)
{
    return one > two ? two : one;
}
ExecResult StartProcess(std::string file, std::string cmd, int timeoutInSecs = 0)
{
    ExecResult result{0};
    result.exitcode = 1;
    string strResult;
    std::stringstream ss;
    HANDLE hPipeRead, hPipeWrite;

    SECURITY_ATTRIBUTES saAttr = {sizeof(SECURITY_ATTRIBUTES)};
    saAttr.bInheritHandle = TRUE; // Pipe handles are inherited by child process.
    saAttr.lpSecurityDescriptor = NULL;

    // Create a pipe to get results from child's stdout.
    if (!CreatePipe(&hPipeRead, &hPipeWrite, &saAttr, 0))
    {
        return result;
    }
    cmd = "\"" + file + "\" " + cmd;
    STARTUPINFOA si = {sizeof(STARTUPINFOW)};
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.hStdOutput = hPipeWrite;
    si.hStdError = hPipeWrite;
    si.wShowWindow = SW_HIDE; // Prevents cmd window from flashing.
    // Requires STARTF_USESHOWWINDOW in dwFlags.

    PROCESS_INFORMATION pi = {0};

    BOOL fSuccess = CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (!fSuccess)
    {
        CloseHandle(hPipeWrite);
        CloseHandle(hPipeRead);
        return result;
    }

    bool bProcessEnded = false;
    auto start = std::time(NULL);
    auto now = start;
    for (; !bProcessEnded;)
    {
        // Give some timeslice (50 ms), so we won't waste 100% CPU.
        bProcessEnded = WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0;
        if (timeoutInSecs > 0)
        {
            now = std::time(NULL);
            if ((now - timeoutInSecs) > start)
            {
                TerminateProcess(pi.hProcess, 0);
            }
        }
        // Even if process exited - we continue reading, if
        // there is some data available over pipe.
        for (;;)
        {
            char buf[1024];
            DWORD dwRead = 0;
            DWORD dwAvail = 0;

            if (!::PeekNamedPipe(hPipeRead, NULL, 0, NULL, &dwAvail, NULL))
                break;

            if (!dwAvail) // No data available, return
                break;

            if (!::ReadFile(hPipeRead, buf, ::minimum(sizeof(buf) - 1, (DWORD)dwAvail), &dwRead, NULL) || !dwRead)
                // Error, the child process might ended
                break;

            buf[dwRead] = 0;
            ss << buf;
        }
    } // for
    strResult = ss.str();
    CloseHandle(hPipeWrite);
    CloseHandle(hPipeRead);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    result.exitcode = code;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    result.result = strResult;
    return result;
}
void KillProcessByPID(int pid)
{

    std::stringstream s;
    s << "/PID " << pid << " /F";
    std::string cmd = s.str();
    auto res = StartProcess(("taskkill"), cmd);
}
static bool endsWith(const std::string &str, const std::string &suffix)
{
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

vector<WindowObject> GetIDMWindows()
{

    vector<WindowObject> windows;
    vector<WindowObject> idm_windows;
    windows.reserve(100);
    EnumWindows(WindowProc, (LONG_PTR)(&windows));
    string idm = "IDMan.exe";
    for (auto &win : windows)
    {
        auto pos = ::GetWindowLong(win.window, GWL_EXSTYLE);
        bool alwaysOnTop = (pos & WS_EX_TOPMOST) == WS_EX_TOPMOST;

        if (endsWith(win.filePath, idm) && alwaysOnTop)
        {
            if (IsWindowVisible(win.window))
            {
                idm_windows.push_back(win);
            }
        }
    }
    return idm_windows;
}
void DestroyWindows()
{
    for (auto &win : GetIDMWindows())
    {
        SendMessage(win.window, WM_CLOSE, 0, 0);
    }
}

int main(int argc, char **argv)
{
    while (true)
    {
        DestroyWindows();
        Sleep(500);
    }
    return 0;
}