#include <Windows.h>
#include <string>
#include <iostream>
#include <vector>
#include <sstream>
#include <algorithm>

using std::string;
using std::vector;
std::string ToLowerCase(const std::string &input)
{
    std::string result = input; // Create a copy of the input string

    // Use std::transform to convert each character to lowercase
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });

    return result;
}
bool StringContains(const std::string &mainString, const std::string &subString)
{
    // Use std::string::find to check if subString exists in mainString
    return mainString.find(subString) != std::string::npos;
}
BOOL CALLBACK EnumChildProc(HWND hwndChild, LPARAM lParam)
{
    std::string *pTexts = reinterpret_cast<std::string *>(lParam);

    char buffer[256];
    if (GetWindowTextA(hwndChild, buffer, sizeof(buffer)))
    {
        if (strlen(buffer) > 0)
        {
            pTexts->append(std::string(buffer) + "\n");
        }
    }

    return TRUE; // Continue enumeration
}

// Function to get all text displayed by a window and its children
std::string GetAllWindowText(HWND hwnd)
{
    std::string texts;

    // Get the text of the main window
    char buffer[256];
    if (GetWindowTextA(hwnd, buffer, sizeof(buffer)))
    {
        if (strlen(buffer) > 0)
        {
            texts.append(std::string(buffer) + "\n");
        }
    }

    // Enumerate all child windows and get their text
    EnumChildWindows(hwnd, EnumChildProc, reinterpret_cast<LPARAM>(&texts));

    return texts;
}
bool startsWith(const std::string &str, const std::string &prefix)
{
    // Check if the prefix is longer than the string
    if (prefix.length() > str.length())
    {
        return false;
    }

    // Compare the prefix with the beginning of the string
    return str.compare(0, prefix.length(), prefix) == 0;
}
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
    bool topMost;
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
    auto pos = ::GetWindowLong(win, GWL_EXSTYLE);
    bool alwaysOnTop = (pos & WS_EX_TOPMOST) == WS_EX_TOPMOST;

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
    obj.topMost = alwaysOnTop;
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

        // if (endsWith(win.filePath, idm) && win.topMost)
        if (endsWith(win.filePath, idm))
        {
            if (IsWindowVisible(win.window))
            {
                // Internet Explorer_Hidden
                if (win.title != "Internet Explorer_Hidden" && win.title != "Download File Info" && win.title != "Download complete" && !startsWith(win.title, "IDM drop target"))
                {
                    auto text = ToLowerCase(GetAllWindowText(win.window));

                    bool isGood = win.topMost;
                    if (StringContains(text, "register") || StringContains(text, "left to use internet download manager"))
                    {
                        isGood = true;
                    }
                    else if (StringContains(text, "Settings"))
                    {
                        isGood = false;
                    }
                    else if (win.className == "#32768" && win.topMost)
                    {
                        isGood = false;
                    }
                    else if (win.title == "IDM" && win.topMost)
                    {
                        isGood = false;
                    }
                    if (isGood)
                    {
                        idm_windows.push_back(win);
                    }
                }
            }
        }
    }
    return idm_windows;
}
void CloseWindowHandle(HWND window)
{
    ShowWindow(window,SW_HIDE);
    auto res = SendMessageA(window, WM_CLOSE, 0, 0);

}

int main(int argc, char **argv)
{

    auto handle = CreateMutexA(NULL, TRUE, "Global Idm auto close");
    auto ret = GetLastError();
    if (ret != ERROR_ALREADY_EXISTS && handle != INVALID_HANDLE_VALUE && handle != NULL)
    {
        auto sleep = 1000;
        while (true)
        {
            auto windows = GetIDMWindows();
            if (windows.size() > 0)
            {
                sleep = 500;
                for (auto &win : windows)
                {
                    // if (win.title.empty())
                    CloseWindowHandle(win.window);
                    Sleep(0);
                }
            }
            else
            {
                sleep = 1000;
            }

            Sleep(sleep);
        }
    }
    return 0;
}