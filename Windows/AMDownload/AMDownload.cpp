#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")

namespace {

constexpr wchar_t kMainClassName[] = L"AMDownloadMainWindow";
constexpr wchar_t kContentClassName[] = L"AMDownloadContentWindow";

constexpr int kUrlEdit = 1001;
constexpr int kPasteButton = 1002;
constexpr int kPlatformLabel = 1003;

constexpr int kMoreTagsButton = 1010;
constexpr int kExplicitCheck = 1011;

constexpr int kOutputEdit = 1020;
constexpr int kChooseButton = 1021;

constexpr int kLogEdit = 1030;

constexpr int kStatusLabel = 1040;
constexpr int kShowDownloadsButton = 1041;
constexpr int kDownloadButton = 1042;
constexpr int kCancelButton = 1043;

constexpr UINT kAppLogMessage = WM_APP + 1;
constexpr UINT kAppCompleteMessage = WM_APP + 2;

constexpr COLORREF kAccent = RGB(35, 121, 219);
constexpr COLORREF kAccentDark = RGB(26, 86, 158);
constexpr COLORREF kMutedText = RGB(102, 112, 125);
constexpr COLORREF kWarningText = RGB(178, 102, 12);
constexpr COLORREF kCardFill = RGB(248, 250, 253);
constexpr COLORREF kCardBorder = RGB(224, 230, 238);

struct MetadataRow {
    HWND label = nullptr;
    HWND edit = nullptr;
    bool advanced = false;
};

struct DownloadResult {
    DWORD exitCode = 1;
    bool cancelled = false;
};

std::wstring Trim(const std::wstring& value) {
    const auto first = std::find_if(value.begin(), value.end(), [](wchar_t ch) {
        return !iswspace(ch);
    });
    if (first == value.end()) {
        return L"";
    }
    const auto last = std::find_if(value.rbegin(), value.rend(), [](wchar_t ch) {
        return !iswspace(ch);
    }).base();
    return std::wstring(first, last);
}

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

std::wstring EnvironmentValue(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return L"";
    }
    std::wstring result(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, result.data(), required);
    if (written == 0) {
        return L"";
    }
    result.resize(written);
    return result;
}

std::wstring ModuleDirectory() {
    std::vector<wchar_t> buffer(260);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return L".";
        }
        if (length < buffer.size() - 1) {
            std::filesystem::path path(std::wstring(buffer.data(), length));
            return path.parent_path().wstring();
        }
        buffer.resize(buffer.size() * 2);
    }
}

bool IsFile(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    return !error && std::filesystem::is_regular_file(status);
}

std::vector<std::wstring> SearchDirectories() {
    std::vector<std::wstring> directories;
    directories.push_back(ModuleDirectory());
    directories.push_back((std::filesystem::path(ModuleDirectory()) / L"tools").wstring());

    const std::wstring pathValue = EnvironmentValue(L"PATH");
    std::size_t start = 0;
    while (start <= pathValue.size()) {
        const std::size_t end = pathValue.find(L';', start);
        const std::wstring directory = Trim(pathValue.substr(start, end == std::wstring::npos ? end : end - start));
        if (!directory.empty()) {
            directories.push_back(directory);
        }
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }

    std::vector<std::wstring> unique;
    for (const auto& directory : directories) {
        const std::wstring normalized = Lowercase(directory);
        const bool alreadyPresent = std::any_of(unique.begin(), unique.end(), [&](const std::wstring& existing) {
            return Lowercase(existing) == normalized;
        });
        if (!alreadyPresent) {
            unique.push_back(directory);
        }
    }
    return unique;
}

std::wstring FindExecutable(const std::wstring& baseName) {
    std::vector<std::wstring> names = {baseName, baseName + L".exe"};
    for (const auto& directory : SearchDirectories()) {
        for (const auto& name : names) {
            const std::filesystem::path candidate = std::filesystem::path(directory) / name;
            if (IsFile(candidate)) {
                return candidate.wstring();
            }
        }
    }
    return L"";
}

std::wstring DefaultDownloadsDirectory() {
    PWSTR rawPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &rawPath))) {
        std::wstring result(rawPath);
        CoTaskMemFree(rawPath);
        return result;
    }

    const std::wstring profile = EnvironmentValue(L"USERPROFILE");
    if (!profile.empty()) {
        return (std::filesystem::path(profile) / L"Downloads").wstring();
    }
    return L".";
}

std::wstring ClipboardText(HWND owner) {
    if (!OpenClipboard(owner)) {
        return L"";
    }

    std::wstring value;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* raw = static_cast<const wchar_t*>(GlobalLock(data))) {
            value.assign(raw);
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return value;
}

std::wstring ExtractHost(const std::wstring& value) {
    const std::wstring url = Trim(value);
    const std::wstring lower = Lowercase(url);
    std::size_t hostStart = std::wstring::npos;
    if (lower.rfind(L"https://", 0) == 0) {
        hostStart = 8;
    } else if (lower.rfind(L"http://", 0) == 0) {
        hostStart = 7;
    }
    if (hostStart == std::wstring::npos || hostStart >= lower.size()) {
        return L"";
    }

    std::size_t hostEnd = lower.find_first_of(L"/?#", hostStart);
    if (hostEnd == std::wstring::npos) {
        hostEnd = lower.size();
    }
    std::wstring authority = lower.substr(hostStart, hostEnd - hostStart);
    const std::size_t at = authority.rfind(L'@');
    if (at != std::wstring::npos) {
        authority = authority.substr(at + 1);
    }
    if (authority.empty()) {
        return L"";
    }

    if (authority.front() == L'[') {
        const std::size_t closing = authority.find(L']');
        if (closing != std::wstring::npos) {
            authority = authority.substr(0, closing + 1);
        }
    } else if (const std::size_t colon = authority.find(L':'); colon != std::wstring::npos) {
        authority.resize(colon);
    }
    return authority;
}

bool HostMatches(const std::wstring& host, const std::wstring& domain) {
    return host == domain || (host.size() > domain.size()
        && host.compare(host.size() - domain.size(), domain.size(), domain) == 0
        && host[host.size() - domain.size() - 1] == L'.');
}

std::wstring PlatformName(const std::wstring& value) {
    const std::wstring host = ExtractHost(value);
    if (host.empty()) {
        return L"";
    }
    if (HostMatches(host, L"soundcloud.com")) return L"SoundCloud";
    if (HostMatches(host, L"bandcamp.com")) return L"Bandcamp";
    if (HostMatches(host, L"audiomack.com")) return L"Audiomack";
    if (HostMatches(host, L"mixcloud.com")) return L"Mixcloud";
    if (HostMatches(host, L"hearthis.at")) return L"HearThisAt";
    if (HostMatches(host, L"vocaroo.com") || HostMatches(host, L"voca.ro")) return L"Vocaroo";
    if (HostMatches(host, L"instagram.com") || HostMatches(host, L"instagr.am")) return L"Instagram";
    if (HostMatches(host, L"x.com") || HostMatches(host, L"twitter.com")) return L"X";
    return L"";
}

bool IsValidWebUrl(const std::wstring& value) {
    return !ExtractHost(value).empty();
}

bool IsValidIndex(const std::wstring& value) {
    if (value.empty()) {
        return true;
    }
    bool sawSlash = false;
    bool sawDigitBeforeSlash = false;
    bool sawDigitAfterSlash = false;
    for (wchar_t ch : value) {
        if (ch == L'/' && !sawSlash) {
            if (!sawDigitBeforeSlash) {
                return false;
            }
            sawSlash = true;
            continue;
        }
        if (ch < L'0' || ch > L'9') {
            return false;
        }
        if (sawSlash) {
            sawDigitAfterSlash = true;
        } else {
            sawDigitBeforeSlash = true;
        }
    }
    return sawDigitBeforeSlash && (!sawSlash || sawDigitAfterSlash);
}

std::wstring QuoteCommandLineArgument(const std::wstring& value) {
    if (value.empty()) {
        return L"\"\"";
    }

    const bool needsQuotes = value.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needsQuotes) {
        return value;
    }

    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

std::wstring BuildCommandLine(const std::wstring& executable, const std::vector<std::wstring>& arguments) {
    std::wstring command = QuoteCommandLineArgument(executable);
    for (const auto& argument : arguments) {
        command.push_back(L' ');
        command += QuoteCommandLineArgument(argument);
    }
    return command;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required > 0) {
        std::wstring result(required, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), required);
        return result;
    }

    const int fallbackRequired = MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (fallbackRequired <= 0) {
        return L"";
    }
    std::wstring result(fallbackRequired, L'\0');
    MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), result.data(), fallbackRequired);
    return result;
}

std::wstring WindowsErrorMessage(DWORD error) {
    wchar_t* raw = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<LPWSTR>(&raw),
        0,
        nullptr);
    if (length == 0 || raw == nullptr) {
        return L"Windows error " + std::to_wstring(error);
    }
    std::wstring result(raw, length);
    LocalFree(raw);
    return Trim(result);
}

std::vector<std::wstring> OutputFilesFromLog(const std::string& output, const std::wstring& workingDirectory) {
    constexpr char marker[] = "__AM_FILE__:";
    std::vector<std::wstring> files;
    std::size_t searchFrom = 0;
    while (searchFrom < output.size()) {
        const std::size_t markerPosition = output.find(marker, searchFrom);
        if (markerPosition == std::string::npos) {
            break;
        }
        const std::size_t fileStart = markerPosition + sizeof(marker) - 1;
        std::size_t fileEnd = output.find_first_of("\r\n", fileStart);
        if (fileEnd == std::string::npos) {
            fileEnd = output.size();
        }
        std::string rawPath = output.substr(fileStart, fileEnd - fileStart);
        while (!rawPath.empty() && (rawPath.front() == ' ' || rawPath.front() == '\t')) rawPath.erase(rawPath.begin());
        while (!rawPath.empty() && (rawPath.back() == ' ' || rawPath.back() == '\t')) rawPath.pop_back();
        if (!rawPath.empty()) {
            std::filesystem::path path(Utf8ToWide(rawPath));
            if (path.is_relative()) {
                path = std::filesystem::path(workingDirectory) / path;
            }
            const std::wstring normalized = path.lexically_normal().wstring();
            if (std::find(files.begin(), files.end(), normalized) == files.end()) {
                files.push_back(normalized);
            }
        }
        searchFrom = fileEnd + 1;
    }
    return files;
}

class MainWindow {
public:
    MainWindow() = default;

    ~MainWindow() {
        StopDownload();
        if (downloadWorker_.joinable()) {
            downloadWorker_.join();
        }
        DeleteFont(fontNormal_);
        DeleteFont(fontSmall_);
        DeleteFont(fontBold_);
        DeleteFont(fontMono_);
        DeleteFont(fontTitle_);
    }

    bool Create(HINSTANCE instance, int showCommand) {
        instance_ = instance;

        WNDCLASSEXW mainClass{};
        mainClass.cbSize = sizeof(mainClass);
        mainClass.style = CS_HREDRAW | CS_VREDRAW;
        mainClass.lpfnWndProc = &MainWindow::WindowProc;
        mainClass.cbWndExtra = sizeof(void*);
        mainClass.hInstance = instance_;
        mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        mainClass.hbrBackground = nullptr;
        mainClass.lpszClassName = kMainClassName;
        mainClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        mainClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
        if (!RegisterClassExW(&mainClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        WNDCLASSEXW contentClass{};
        contentClass.cbSize = sizeof(contentClass);
        contentClass.style = CS_HREDRAW | CS_VREDRAW;
        contentClass.lpfnWndProc = &MainWindow::ContentProc;
        contentClass.cbWndExtra = sizeof(void*);
        contentClass.hInstance = instance_;
        contentClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        contentClass.hbrBackground = nullptr;
        contentClass.lpszClassName = kContentClassName;
        if (!RegisterClassExW(&contentClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        hwnd_ = CreateWindowExW(
            0,
            kMainClassName,
            L"AM Download",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            780,
            820,
            nullptr,
            nullptr,
            instance_,
            this);
        if (!hwnd_) {
            return false;
        }

        ShowWindow(hwnd_, showCommand);
        UpdateWindow(hwnd_);
        return true;
    }

    int Run() {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

    void StartDownload() {
        if (isRunning_) {
            return;
        }

        if (downloadWorker_.joinable()) {
            downloadWorker_.join();
        }

        const std::wstring url = Trim(ControlText(urlEdit_));
        if (!IsValidWebUrl(url)) {
            ShowError(L"Enter a valid web URL.");
            return;
        }

        const std::wstring track = Trim(ControlText(metadataRows_[7].edit));
        const std::wstring disc = Trim(ControlText(metadataRows_[8].edit));
        if (!IsValidIndex(track)) {
            ShowError(L"Track must be a number or number/total, such as 3 or 3/12.");
            return;
        }
        if (!IsValidIndex(disc)) {
            ShowError(L"Disc must be a number or number/total, such as 1 or 1/2.");
            return;
        }

        RefreshDependencies();
        if (ytDlpPath_.empty() || ffmpegPath_.empty() || atomicParsleyPath_.empty()) {
            ShowError(L"Install yt-dlp, ffmpeg, and AtomicParsley, then reopen the app.\n\nThe Windows build searches the app folder, an app\\tools folder, and PATH.");
            return;
        }

        std::error_code directoryError;
        std::filesystem::create_directories(outputDirectory_, directoryError);
        if (directoryError) {
            ShowError(L"Could not create the download folder:\n\n" + outputDirectory_);
            return;
        }

        std::vector<std::wstring> arguments = {
            L"--newline",
            L"--progress",
            L"--no-colors",
            L"--windows-filenames",
            L"--format", L"bestaudio[ext=m4a]/bestaudio[ext=aac]/bestaudio/best",
            L"--extract-audio",
            L"--audio-format", L"m4a",
            L"--audio-quality", L"0",
            L"--embed-thumbnail",
            L"--embed-metadata",
            L"--embed-chapters",
            L"--add-metadata",
            L"--ffmpeg-location", std::filesystem::path(ffmpegPath_).parent_path().wstring(),
            L"--print", L"after_move:__AM_FILE__:%(filepath)s"
        };

        const std::wstring lowerUrl = Lowercase(url);
        const std::wstring outputTemplate =
            (lowerUrl.find(L"instagram.com") != std::wstring::npos
                || lowerUrl.find(L"instagr.am") != std::wstring::npos
                || lowerUrl.find(L"x.com") != std::wstring::npos
                || lowerUrl.find(L"twitter.com") != std::wstring::npos)
            ? L"%(title).100B.%(ext)s"
            : (lowerUrl.find(L"/sets/") != std::wstring::npos
                ? L"%(playlist_title)s/%(title)s.%(ext)s"
                : L"%(title)s.%(ext)s");
        arguments.push_back(L"--output");
        arguments.push_back(outputTemplate);
        arguments.push_back(url);

        std::vector<std::pair<std::wstring, std::wstring>> tagOverrides;
        AddTagOverride(tagOverrides, L"--title", metadataRows_[0].edit);
        AddTagOverride(tagOverrides, L"--artist", metadataRows_[1].edit);
        AddTagOverride(tagOverrides, L"--album", metadataRows_[2].edit);
        AddTagOverride(tagOverrides, L"--albumArtist", metadataRows_[3].edit);
        AddTagOverride(tagOverrides, L"--genre", metadataRows_[4].edit);
        AddTagOverride(tagOverrides, L"--composer", metadataRows_[5].edit);
        AddTagOverride(tagOverrides, L"--year", metadataRows_[6].edit);
        AddTagOverride(tagOverrides, L"--tracknum", metadataRows_[7].edit);
        AddTagOverride(tagOverrides, L"--disk", metadataRows_[8].edit);
        if (SendMessageW(explicitCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            tagOverrides.emplace_back(L"--advisory", L"explicit");
        }

        const std::wstring workingDirectory = outputDirectory_;
        const std::wstring downloader = ytDlpPath_;
        const std::wstring metadataTool = atomicParsleyPath_;
        const std::vector<std::pair<std::wstring, std::wstring>> overrides = tagOverrides;

        cancelRequested_.store(false);
        isRunning_ = true;
        UpdateFooterButtons();
        UpdateStatus(L"Downloading…");
        logText_ = L"Downloading " + url + L"\r\n\r\n";
        UpdateLogControl();

        downloadWorker_ = std::thread([this, downloader, metadataTool, workingDirectory, arguments, overrides]() {
            DWORD exitCode = 1;
            std::string downloaderOutput;
            const bool started = RunLoggedProcess(downloader, arguments, workingDirectory, &downloaderOutput, exitCode);
            bool cancelled = cancelRequested_.load();

            if (started && exitCode == 0 && !cancelled && !overrides.empty()) {
                const auto outputFiles = OutputFilesFromLog(downloaderOutput, workingDirectory);
                if (outputFiles.empty()) {
                    PostLog(L"\r\nMetadata overrides were requested, but yt-dlp did not report an output file.\r\n");
                }

                for (const auto& file : outputFiles) {
                    if (cancelRequested_.load()) {
                        cancelled = true;
                        break;
                    }

                    std::vector<std::wstring> tagArguments = {file};
                    for (const auto& override : overrides) {
                        tagArguments.push_back(override.first);
                        tagArguments.push_back(override.second);
                    }
                    tagArguments.push_back(L"--overWrite");

                    DWORD tagExitCode = 1;
                    std::string tagOutput;
                    const bool tagStarted = RunLoggedProcess(metadataTool, tagArguments, workingDirectory, &tagOutput, tagExitCode);
                    if (!tagStarted || tagExitCode != 0) {
                        exitCode = tagExitCode == 0 ? 1 : tagExitCode;
                        PostLog(L"\r\nAtomicParsley could not apply the requested metadata.\r\n");
                        break;
                    }
                }
            }

            if (cancelRequested_.load()) {
                cancelled = true;
            }
            PostComplete(exitCode, cancelled);
        });
    }

private:
    static void DeleteFont(HFONT& font) {
        if (font) {
            DeleteObject(font);
            font = nullptr;
        }
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<MainWindow*>(create->lpCreateParams);
            self->hwnd_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        return self->HandleWindowMessage(message, wParam, lParam);
    }

    static LRESULT CALLBACK ContentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<MainWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        return self->HandleContentMessage(window, message, wParam, lParam);
    }

    static LRESULT CALLBACK UrlSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<MainWindow*>(data);
        if (message == WM_KEYDOWN && wParam == VK_RETURN) {
            self->StartDownload();
            return 0;
        }
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(window, &MainWindow::UrlSubclassProc, 1);
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }

    LRESULT HandleWindowMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            if (!InitializeControls()) {
                return -1;
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = Scale(680);
            info->ptMinTrackSize.y = Scale(700);
            return 0;
        }
        case WM_SIZE:
            Layout();
            return 0;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wParam) == 0 ? 96 : HIWORD(wParam);
            CreateFonts();
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            InvalidateRect(hwnd_, nullptr, TRUE);
            return 0;
        }
        case WM_COMMAND:
            return HandleCommand(wParam, lParam);
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            return HandleControlColor(message, reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam));
        case WM_PAINT:
            PaintWindow();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case kAppLogMessage: {
            auto* text = reinterpret_cast<std::wstring*>(lParam);
            if (text) {
                AppendLog(*text);
                delete text;
            }
            return 0;
        }
        case kAppCompleteMessage: {
            auto* result = reinterpret_cast<DownloadResult*>(lParam);
            if (result) {
                FinishDownload(*result);
                delete result;
            }
            return 0;
        }
        case WM_CLOSE:
            if (isRunning_) {
                StopDownload();
            }
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            StopDownload();
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        default:
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        }
    }

    LRESULT HandleContentMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_COMMAND:
            return HandleCommand(wParam, lParam);
        case WM_VSCROLL:
            ScrollFromCommand(LOWORD(wParam));
            return 0;
        case WM_MOUSEWHEEL:
            ScrollContentTo(scrollOffset_ - GET_WHEEL_DELTA_WPARAM(wParam) * Scale(3) / WHEEL_DELTA);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_PRIOR) {
                ScrollContentTo(scrollOffset_ - GetContentViewportHeight());
                return 0;
            }
            if (wParam == VK_NEXT) {
                ScrollContentTo(scrollOffset_ + GetContentViewportHeight());
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN:
            return HandleControlColor(message, reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam));
        case WM_PAINT:
            PaintContent(window);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            LayoutContent();
            return 0;
        default:
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    bool InitializeControls() {
        dpi_ = GetDpiForWindow(hwnd_);
        if (dpi_ == 0) dpi_ = 96;
        CreateFonts();
        outputDirectory_ = DefaultDownloadsDirectory();

        contentHost_ = CreateWindowExW(
            0,
            kContentClassName,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, 0, 0,
            hwnd_,
            nullptr,
            instance_,
            this);
        if (!contentHost_) {
            return false;
        }

        sourceHeading_ = CreateStatic(contentHost_, L"Source", 1100, fontBold_);
        metadataHeading_ = CreateStatic(contentHost_, L"Metadata", 1101, fontBold_);
        saveHeading_ = CreateStatic(contentHost_, L"Save To", 1102, fontBold_);
        activityHeading_ = CreateStatic(contentHost_, L"Activity", 1103, fontBold_);
        descriptionLabel_ = CreateStatic(contentHost_, L"Source metadata is kept automatically. Enter only the values you want to replace.", 1104, fontSmall_);
        supportedLabel_ = CreateStatic(contentHost_, L"SoundCloud · Bandcamp · Audiomack · Mixcloud · HearThisAt · Vocaroo · Instagram · X", 1105, fontSmall_);

        urlEdit_ = CreateEdit(contentHost_, L"", kUrlEdit, ES_AUTOHSCROLL);
        SetCueBanner(urlEdit_, L"Paste a supported music or social link");
        SetWindowSubclass(urlEdit_, &MainWindow::UrlSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
        pasteButton_ = CreateButton(contentHost_, L"Paste", kPasteButton);
        platformLabel_ = CreateStatic(contentHost_, L"", kPlatformLabel, fontSmall_);
        ShowWindow(platformLabel_, SW_HIDE);

        const std::vector<std::pair<std::wstring, std::wstring>> rows = {
            {L"Title", L"Keep source value"},
            {L"Artist", L"Keep source value"},
            {L"Album", L"Keep source value"},
            {L"Album Artist", L"Keep source value"},
            {L"Genre", L"Keep source value"},
            {L"Composer", L"Keep source value"},
            {L"Release Date", L"Keep source value (e.g. 2026)"},
            {L"Track", L"Keep source value (e.g. 3/12)"},
            {L"Disc", L"Keep source value (e.g. 1/2)"}
        };
        for (std::size_t index = 0; index < rows.size(); ++index) {
            MetadataRow row;
            row.label = CreateStatic(contentHost_, rows[index].first.c_str(), 1120 + static_cast<int>(index), fontSmall_, SS_RIGHT);
            row.edit = CreateEdit(contentHost_, L"", 1140 + static_cast<int>(index), ES_AUTOHSCROLL);
            SetCueBanner(row.edit, rows[index].second);
            row.advanced = index >= 5;
            metadataRows_.push_back(row);
        }

        moreTagsButton_ = CreateButton(contentHost_, L"More Apple Music tags", kMoreTagsButton, BS_AUTOCHECKBOX | BS_LEFTTEXT);
        explicitCheck_ = CreateButton(contentHost_, L"Mark as explicit (off keeps the source value)", kExplicitCheck, BS_AUTOCHECKBOX | BS_LEFTTEXT);

        saveFolderLabel_ = CreateStatic(contentHost_, L"Download folder", 1160, fontSmall_);
        outputEdit_ = CreateEdit(contentHost_, outputDirectory_.c_str(), kOutputEdit, ES_READONLY | ES_AUTOHSCROLL);
        chooseButton_ = CreateButton(contentHost_, L"Choose…", kChooseButton);

        logEdit_ = CreateEdit(contentHost_, L"", kLogEdit, ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_WANTRETURN | WS_VSCROLL);
        SendMessageW(logEdit_, EM_SETLIMITTEXT, 2 * 1024 * 1024, 0);
        SendMessageW(logEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(fontMono_), TRUE);
        UpdateLogControl();

        statusLabel_ = CreateStatic(hwnd_, L"", kStatusLabel, fontSmall_);
        showDownloadsButton_ = CreateButton(hwnd_, L"Show Downloads", kShowDownloadsButton);
        downloadButton_ = CreateButton(hwnd_, L"Download", kDownloadButton, BS_DEFPUSHBUTTON);
        cancelButton_ = CreateButton(hwnd_, L"Cancel", kCancelButton);
        ShowWindow(cancelButton_, SW_HIDE);

        RefreshDependencies();
        UpdateDetectedPlatform();
        AutoPasteSupportedUrl();
        Layout();
        SetFocus(urlEdit_);
        return true;
    }

    HWND CreateStatic(HWND parent, const wchar_t* text, int id, HFONT font, DWORD style = SS_LEFT) {
        HWND control = CreateWindowExW(
            0,
            L"STATIC",
            text,
            WS_CHILD | WS_VISIBLE | style,
            0, 0, 0, 0,
            parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_,
            nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return control;
    }

    HWND CreateEdit(HWND parent, const wchar_t* text, int id, DWORD style) {
        HWND control = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
            0, 0, 0, 0,
            parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_,
            nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(fontNormal_), TRUE);
        return control;
    }

    HWND CreateButton(HWND parent, const wchar_t* text, int id, DWORD extraStyle = 0) {
        HWND control = CreateWindowExW(
            0,
            L"BUTTON",
            text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | extraStyle,
            0, 0, 0, 0,
            parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_,
            nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(fontNormal_), TRUE);
        return control;
    }

    void SetCueBanner(HWND edit, const std::wstring& text) {
        SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(text.c_str()));
    }

    int Scale(int value) const {
        return MulDiv(value, dpi_, 96);
    }

    int GetContentViewportHeight() const {
        RECT rect{};
        if (contentHost_) {
            GetClientRect(contentHost_, &rect);
        }
        return std::max(1, static_cast<int>(rect.bottom - rect.top));
    }

    void CreateFonts() {
        DeleteFont(fontNormal_);
        DeleteFont(fontSmall_);
        DeleteFont(fontBold_);
        DeleteFont(fontMono_);
        DeleteFont(fontTitle_);

        const int normalHeight = -MulDiv(10, dpi_, 72);
        const int smallHeight = -MulDiv(9, dpi_, 72);
        const int titleHeight = -MulDiv(20, dpi_, 72);
        fontNormal_ = CreateFontW(normalHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        fontSmall_ = CreateFontW(smallHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        fontBold_ = CreateFontW(normalHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        fontMono_ = CreateFontW(smallHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");
        fontTitle_ = CreateFontW(titleHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        if (!hwnd_) {
            return;
        }
        for (HWND control : {sourceHeading_, metadataHeading_, saveHeading_, activityHeading_, descriptionLabel_, supportedLabel_, urlEdit_, pasteButton_, platformLabel_, moreTagsButton_, explicitCheck_, saveFolderLabel_, outputEdit_, chooseButton_, logEdit_, statusLabel_, showDownloadsButton_, downloadButton_, cancelButton_}) {
            if (control) {
                HFONT font = fontNormal_;
                if (control == sourceHeading_ || control == metadataHeading_ || control == saveHeading_ || control == activityHeading_) font = fontBold_;
                if (control == descriptionLabel_ || control == supportedLabel_ || control == platformLabel_ || control == saveFolderLabel_ || control == statusLabel_) font = fontSmall_;
                if (control == logEdit_) font = fontMono_;
                if (control == statusLabel_) font = fontSmall_;
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
        }
        for (const auto& row : metadataRows_) {
            if (row.label) SendMessageW(row.label, WM_SETFONT, reinterpret_cast<WPARAM>(fontSmall_), TRUE);
            if (row.edit) SendMessageW(row.edit, WM_SETFONT, reinterpret_cast<WPARAM>(fontNormal_), TRUE);
        }
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void Layout() {
        if (!hwnd_) {
            return;
        }
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        const int headerHeight = Scale(88);
        const int footerHeight = Scale(60);
        const int contentHeight = std::max(0, height - headerHeight - footerHeight);

        if (contentHost_) {
            MoveWindow(contentHost_, 0, headerHeight, width, contentHeight, TRUE);
        }

        const int footerTop = std::max(headerHeight, height - footerHeight);
        const int buttonHeight = Scale(30);
        const int margin = Scale(18);
        const int gap = Scale(8);
        const int downloadWidth = Scale(104);
        const int cancelWidth = Scale(88);
        const int showWidth = Scale(132);
        const int buttonY = footerTop + Scale(14);
        int right = width - margin;

        MoveWindow(downloadButton_, right - downloadWidth, buttonY, downloadWidth, buttonHeight, TRUE);
        right -= downloadWidth + gap;
        MoveWindow(cancelButton_, right - cancelWidth, buttonY, cancelWidth, buttonHeight, TRUE);
        right -= cancelWidth + gap;
        MoveWindow(showDownloadsButton_, right - showWidth, buttonY, showWidth, buttonHeight, TRUE);

        MoveWindow(statusLabel_, margin, footerTop + Scale(18), std::max(80, right - margin - gap), Scale(24), TRUE);
        LayoutContent();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void LayoutContent() {
        if (!contentHost_) {
            return;
        }
        RECT client{};
        GetClientRect(contentHost_, &client);
        const int width = client.right - client.left;
        const int pad = Scale(24);
        const int labelX = pad;
        const int labelWidth = Scale(112);
        const int inputX = Scale(150);
        const int right = std::max(inputX + Scale(180), width - pad);
        const int inputWidth = std::max(120, right - inputX);
        const int rowHeight = Scale(30);
        const int rowStep = Scale(38);

        int y = Scale(18);
        const int sourceTop = y - scrollOffset_;
        MoveWindow(sourceHeading_, pad + Scale(12), sourceTop + Scale(10), width - 2 * pad, Scale(24), TRUE);
        MoveWindow(urlEdit_, pad + Scale(12), sourceTop + Scale(43), std::max(140, right - pad - Scale(112)), rowHeight, TRUE);
        MoveWindow(pasteButton_, right - Scale(96), sourceTop + Scale(43), Scale(96), rowHeight, TRUE);
        MoveWindow(platformLabel_, pad + Scale(14), sourceTop + Scale(78), width - 2 * pad, Scale(20), TRUE);
        MoveWindow(supportedLabel_, pad + Scale(14), sourceTop + Scale(102), width - 2 * pad, Scale(20), TRUE);
        const int sourceBottom = y + Scale(136);

        const int metadataTop = sourceBottom + Scale(22);
        const int metadataY = metadataTop - scrollOffset_;
        MoveWindow(metadataHeading_, pad + Scale(12), metadataY + Scale(10), width - 2 * pad, Scale(24), TRUE);
        MoveWindow(descriptionLabel_, pad + Scale(14), metadataY + Scale(40), width - 2 * pad, Scale(22), TRUE);

        const int rowsTop = metadataTop + Scale(76);
        for (std::size_t index = 0; index < metadataRows_.size(); ++index) {
            const bool visible = !metadataRows_[index].advanced || moreTagsExpanded_;
            const int rowY = rowsTop + static_cast<int>(index) * rowStep - scrollOffset_;
            ShowWindow(metadataRows_[index].label, visible ? SW_SHOW : SW_HIDE);
            ShowWindow(metadataRows_[index].edit, visible ? SW_SHOW : SW_HIDE);
            MoveWindow(metadataRows_[index].label, labelX, rowY + Scale(4), labelWidth, Scale(22), TRUE);
            MoveWindow(metadataRows_[index].edit, inputX, rowY, inputWidth, rowHeight, TRUE);
        }

        const int moreY = rowsTop + 5 * rowStep + Scale(6);
        MoveWindow(moreTagsButton_, pad + Scale(14), moreY - scrollOffset_, Scale(220), rowHeight, TRUE);
        const int explicitY = moreY + (moreTagsExpanded_ ? 4 * rowStep + Scale(8) : rowStep);
        MoveWindow(explicitCheck_, pad + Scale(14), explicitY - scrollOffset_, width - 2 * pad, rowHeight, TRUE);

        const int saveTop = explicitY + Scale(42);
        MoveWindow(saveHeading_, pad + Scale(12), saveTop - scrollOffset_ + Scale(10), width - 2 * pad, Scale(24), TRUE);
        MoveWindow(saveFolderLabel_, pad + Scale(14), saveTop - scrollOffset_ + Scale(40), Scale(108), Scale(22), TRUE);
        MoveWindow(outputEdit_, inputX, saveTop - scrollOffset_ + Scale(35), std::max(120, inputWidth - Scale(104)), rowHeight, TRUE);
        MoveWindow(chooseButton_, right - Scale(96), saveTop - scrollOffset_ + Scale(35), Scale(96), rowHeight, TRUE);

        const int activityTop = saveTop + Scale(82);
        MoveWindow(activityHeading_, pad + Scale(12), activityTop - scrollOffset_ + Scale(10), width - 2 * pad, Scale(24), TRUE);
        const int logHeight = Scale(158);
        MoveWindow(logEdit_, pad + Scale(12), activityTop - scrollOffset_ + Scale(40), width - 2 * pad - Scale(24), logHeight, TRUE);

        contentHeight_ = activityTop + Scale(40) + logHeight + Scale(28);
        const int viewportHeight = std::max(0, static_cast<int>(client.bottom - client.top));
        const int maxScroll = std::max(0, contentHeight_ - viewportHeight);
        scrollOffset_ = std::clamp(scrollOffset_, 0, maxScroll);
        SCROLLINFO scrollInfo{sizeof(scrollInfo), SIF_RANGE | SIF_PAGE | SIF_POS};
        scrollInfo.nMin = 0;
        scrollInfo.nMax = std::max(0, contentHeight_ - 1);
        scrollInfo.nPage = static_cast<UINT>(viewportHeight);
        scrollInfo.nPos = scrollOffset_;
        SetScrollInfo(contentHost_, SB_VERT, &scrollInfo, TRUE);

        if (moreTagsButton_) {
            SendMessageW(moreTagsButton_, BM_SETCHECK, moreTagsExpanded_ ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        InvalidateRect(contentHost_, nullptr, TRUE);
    }

    void PaintWindow() {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd_, &paint);
        RECT client{};
        GetClientRect(hwnd_, &client);
        HBRUSH background = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        FillRect(dc, &client, background);
        DeleteObject(background);

        const int headerHeight = Scale(88);
        const int footerHeight = Scale(60);
        RECT header{0, 0, client.right, headerHeight};
        HBRUSH headerBrush = CreateSolidBrush(RGB(247, 250, 254));
        FillRect(dc, &header, headerBrush);
        DeleteObject(headerBrush);

        RECT footer{0, std::max(headerHeight, static_cast<int>(client.bottom - footerHeight)), client.right, client.bottom};
        HBRUSH footerBrush = CreateSolidBrush(RGB(250, 251, 253));
        FillRect(dc, &footer, footerBrush);
        DeleteObject(footerBrush);

        HPEN separator = CreatePen(PS_SOLID, 1, RGB(226, 231, 238));
        HGDIOBJ oldPen = SelectObject(dc, separator);
        MoveToEx(dc, 0, headerHeight - 1, nullptr);
        LineTo(dc, client.right, headerHeight - 1);
        MoveToEx(dc, 0, footer.top, nullptr);
        LineTo(dc, client.right, footer.top);
        SelectObject(dc, oldPen);
        DeleteObject(separator);

        const int iconLeft = Scale(24);
        const int iconTop = Scale(20);
        const int iconSize = Scale(48);
        HBRUSH iconBrush = CreateSolidBrush(kAccent);
        HGDIOBJ oldBrush = SelectObject(dc, iconBrush);
        HPEN noPen = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ oldIconPen = SelectObject(dc, noPen);
        Ellipse(dc, iconLeft, iconTop, iconLeft + iconSize, iconTop + iconSize);
        SelectObject(dc, oldIconPen);
        DeleteObject(noPen);
        SelectObject(dc, oldBrush);
        DeleteObject(iconBrush);

        HPEN arrowPen = CreatePen(PS_SOLID, Scale(3), RGB(255, 255, 255));
        oldPen = SelectObject(dc, arrowPen);
        MoveToEx(dc, iconLeft + iconSize / 2, iconTop + Scale(10), nullptr);
        LineTo(dc, iconLeft + iconSize / 2, iconTop + Scale(31));
        MoveToEx(dc, iconLeft + Scale(15), iconTop + Scale(24), nullptr);
        LineTo(dc, iconLeft + iconSize / 2, iconTop + Scale(33));
        LineTo(dc, iconLeft + Scale(33), iconTop + Scale(24));
        SelectObject(dc, oldPen);
        DeleteObject(arrowPen);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(31, 40, 53));
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, fontTitle_));
        RECT titleRect{Scale(88), Scale(18), client.right - Scale(20), Scale(47)};
        DrawTextW(dc, L"AM Download", -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(dc, fontSmall_);
        SetTextColor(dc, kMutedText);
        RECT subtitleRect{Scale(90), Scale(48), client.right - Scale(20), Scale(74)};
        DrawTextW(dc, L"Create tagged M4A files for Apple Music", -1, &subtitleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(dc, oldFont);

        EndPaint(hwnd_, &paint);
    }

    void PaintContent(HWND window) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        FillRect(dc, &client, background);
        DeleteObject(background);

        const int width = client.right - client.left;
        const int pad = Scale(24);
        const int sourceTop = Scale(18) - scrollOffset_;
        const int sourceBottom = Scale(154) - scrollOffset_;
        const int metadataTop = Scale(176) - scrollOffset_;
        const int rowsTop = metadataTop + Scale(76);
        const int rowStep = Scale(38);
        const int moreY = rowsTop + 5 * rowStep + Scale(6);
        const int explicitY = moreY + (moreTagsExpanded_ ? 4 * rowStep + Scale(8) : rowStep);
        const int saveTop = explicitY + Scale(42);
        const int activityTop = saveTop + Scale(82);
        const int logBottom = activityTop + Scale(40) + Scale(158);

        auto drawCard = [&](int top, int bottom) {
            if (bottom < 0 || top > client.bottom) return;
            RECT rect{pad, top, std::max(pad + 1, width - pad), bottom};
            HBRUSH brush = CreateSolidBrush(kCardFill);
            HPEN pen = CreatePen(PS_SOLID, 1, kCardBorder);
            HGDIOBJ oldBrush = SelectObject(dc, brush);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, Scale(10), Scale(10));
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(pen);
            DeleteObject(brush);
        };

        drawCard(sourceTop, sourceBottom);
        drawCard(metadataTop - Scale(8), (moreTagsExpanded_ ? explicitY : moreY + Scale(38)) + Scale(34));
        drawCard(saveTop, saveTop + Scale(78));
        drawCard(activityTop, logBottom + Scale(12));

        auto drawAccent = [&](int top) {
            RECT accent{pad + Scale(12), top + Scale(10), pad + Scale(15), top + Scale(31)};
            HBRUSH brush = CreateSolidBrush(kAccent);
            FillRect(dc, &accent, brush);
            DeleteObject(brush);
        };
        drawAccent(sourceTop);
        drawAccent(metadataTop);
        drawAccent(saveTop);
        drawAccent(activityTop);

        EndPaint(window, &paint);
    }

    LRESULT HandleCommand(WPARAM wParam, LPARAM lParam) {
        (void)lParam;
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id == kUrlEdit && code == EN_CHANGE) {
            UpdateDetectedPlatform();
        } else if (id == kPasteButton && code == BN_CLICKED) {
            PasteUrl();
        } else if (id == kMoreTagsButton && code == BN_CLICKED) {
            moreTagsExpanded_ = SendMessageW(moreTagsButton_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            LayoutContent();
        } else if (id == kChooseButton && code == BN_CLICKED) {
            ChooseOutputDirectory();
        } else if (id == kDownloadButton && code == BN_CLICKED) {
            StartDownload();
        } else if (id == kCancelButton && code == BN_CLICKED) {
            CancelDownload();
        } else if (id == kShowDownloadsButton && code == BN_CLICKED) {
            ShowDownloads();
        }
        return 0;
    }

    LRESULT HandleControlColor(UINT message, HDC dc, HWND control) {
        if (message == WM_CTLCOLOREDIT) {
            SetBkColor(dc, RGB(255, 255, 255));
            SetTextColor(dc, RGB(35, 42, 52));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(43, 51, 63));
        if (control == descriptionLabel_ || control == supportedLabel_ || control == saveFolderLabel_) {
            SetTextColor(dc, kMutedText);
        } else if (control == platformLabel_) {
            SetTextColor(dc, RGB(43, 118, 76));
        } else if (control == sourceHeading_ || control == metadataHeading_ || control == saveHeading_ || control == activityHeading_) {
            SetTextColor(dc, kAccentDark);
        } else if (control == statusLabel_) {
            SetTextColor(dc, missingDependencies_.empty() ? kMutedText : kWarningText);
        }
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }

    void ScrollFromCommand(int command) {
        SCROLLINFO info{sizeof(info), SIF_ALL};
        GetScrollInfo(contentHost_, SB_VERT, &info);
        int next = info.nPos;
        switch (command) {
        case SB_LINEUP: next -= Scale(42); break;
        case SB_LINEDOWN: next += Scale(42); break;
        case SB_PAGEUP: next -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: next += static_cast<int>(info.nPage); break;
        case SB_TOP: next = 0; break;
        case SB_BOTTOM: next = info.nMax; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: next = info.nTrackPos; break;
        default: return;
        }
        ScrollContentTo(next);
    }

    void ScrollContentTo(int position) {
        const int maxScroll = std::max(0, contentHeight_ - GetContentViewportHeight());
        scrollOffset_ = std::clamp(position, 0, maxScroll);
        LayoutContent();
    }

    std::wstring ControlText(HWND control) const {
        if (!control) return L"";
        const int length = GetWindowTextLengthW(control);
        std::wstring value(length + 1, L'\0');
        if (length > 0) {
            GetWindowTextW(control, value.data(), length + 1);
        }
        value.resize(length);
        return value;
    }

    void AddTagOverride(std::vector<std::pair<std::wstring, std::wstring>>& tags, const std::wstring& flag, HWND edit) {
        const std::wstring value = Trim(ControlText(edit));
        if (!value.empty()) {
            tags.emplace_back(flag, value);
        }
    }

    void UpdateDetectedPlatform() {
        const std::wstring platform = PlatformName(ControlText(urlEdit_));
        if (platform.empty()) {
            ShowWindow(platformLabel_, SW_HIDE);
            SetWindowTextW(platformLabel_, L"");
        } else {
            SetWindowTextW(platformLabel_, (platform + L" link detected").c_str());
            ShowWindow(platformLabel_, SW_SHOW);
        }
        InvalidateRect(contentHost_, nullptr, TRUE);
    }

    void PasteUrl() {
        const std::wstring pasted = Trim(ClipboardText(hwnd_));
        if (!pasted.empty()) {
            SetWindowTextW(urlEdit_, pasted.c_str());
            SetFocus(urlEdit_);
            SendMessageW(urlEdit_, EM_SETSEL, 0, -1);
        }
    }

    void AutoPasteSupportedUrl() {
        if (GetWindowTextLengthW(urlEdit_) != 0) {
            return;
        }
        const std::wstring clipboard = Trim(ClipboardText(hwnd_));
        if (PlatformName(clipboard).empty()) {
            return;
        }
        SetWindowTextW(urlEdit_, clipboard.c_str());
        SendMessageW(urlEdit_, EM_SETSEL, 0, -1);
    }

    void ChooseOutputDirectory() {
        IFileDialog* dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            return;
        }

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dialog->SetTitle(L"Choose download folder");
        dialog->SetOkButtonLabel(L"Choose");

        IShellItem* currentFolder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(outputDirectory_.c_str(), nullptr, IID_PPV_ARGS(&currentFolder)))) {
            dialog->SetFolder(currentFolder);
            currentFolder->Release();
        }

        if (SUCCEEDED(dialog->Show(hwnd_))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR rawPath = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath))) {
                    outputDirectory_ = rawPath;
                    CoTaskMemFree(rawPath);
                    SetWindowTextW(outputEdit_, outputDirectory_.c_str());
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    void ShowDownloads() {
        std::error_code error;
        std::filesystem::create_directories(outputDirectory_, error);
        ShellExecuteW(hwnd_, L"open", outputDirectory_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void RefreshDependencies() {
        ytDlpPath_ = FindExecutable(L"yt-dlp");
        ffmpegPath_ = FindExecutable(L"ffmpeg");
        atomicParsleyPath_ = FindExecutable(L"AtomicParsley");
        missingDependencies_.clear();
        if (ytDlpPath_.empty()) missingDependencies_.push_back(L"yt-dlp");
        if (ffmpegPath_.empty()) missingDependencies_.push_back(L"ffmpeg");
        if (atomicParsleyPath_.empty()) missingDependencies_.push_back(L"AtomicParsley");
        UpdateStatus();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void UpdateStatus(const std::wstring& activity = L"") {
        if (!statusLabel_) return;
        std::wstring status;
        if (!activity.empty()) {
            status = activity + L"  ·  ";
        }
        if (missingDependencies_.empty()) {
            status += L"Required tools are installed";
        } else {
            status += L"Missing: ";
            for (std::size_t i = 0; i < missingDependencies_.size(); ++i) {
                if (i != 0) status += L", ";
                status += missingDependencies_[i];
            }
        }
        SetWindowTextW(statusLabel_, status.c_str());
    }

    void UpdateFooterButtons() {
        EnableWindow(downloadButton_, !isRunning_);
        EnableWindow(showDownloadsButton_, !isRunning_);
        ShowWindow(cancelButton_, isRunning_ ? SW_SHOW : SW_HIDE);
        Layout();
    }

    void UpdateLogControl() {
        if (!logEdit_) return;
        if (logText_.empty()) {
            SetWindowTextW(logEdit_, L"Ready to download.");
        } else {
            SetWindowTextW(logEdit_, logText_.c_str());
        }
        SendMessageW(logEdit_, static_cast<UINT>(EM_SETSEL), static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        SendMessageW(logEdit_, EM_SCROLLCARET, 0, 0);
    }

    void AppendLog(const std::wstring& text) {
        if (!logEdit_ || text.empty()) return;
        if (logText_.empty()) {
            SetWindowTextW(logEdit_, L"");
        }
        logText_ += text;
        if (logText_.size() > 900000) {
            logText_.erase(0, logText_.size() - 800000);
            SetWindowTextW(logEdit_, logText_.c_str());
        } else {
            SendMessageW(logEdit_, static_cast<UINT>(EM_SETSEL), static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
            SendMessageW(logEdit_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
        }
        SendMessageW(logEdit_, static_cast<UINT>(EM_SETSEL), static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        SendMessageW(logEdit_, EM_SCROLLCARET, 0, 0);
    }

    void ShowError(const std::wstring& message) {
        MessageBoxW(hwnd_, message.c_str(), L"AM Download", MB_OK | MB_ICONERROR);
    }

    void CancelDownload() {
        if (!isRunning_) return;
        cancelRequested_.store(true);
        AppendLog(L"\r\nCancelling…\r\n");
        TerminateCurrentProcess();
        UpdateStatus(L"Cancelling…");
    }

    void StopDownload() {
        cancelRequested_.store(true);
        TerminateCurrentProcess();
    }

    void TerminateCurrentProcess() {
        std::lock_guard<std::mutex> lock(processMutex_);
        if (currentJob_) {
            TerminateJobObject(currentJob_, 1);
        } else if (currentProcess_) {
            TerminateProcess(currentProcess_, 1);
        }
    }

    bool RunLoggedProcess(const std::wstring& executable, const std::vector<std::wstring>& arguments,
        const std::wstring& workingDirectory, std::string* capturedOutput, DWORD& exitCode) {
        SECURITY_ATTRIBUTES securityAttributes{sizeof(securityAttributes), nullptr, TRUE};
        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0)) {
            PostLog(L"\r\nCould not create a process output pipe.\r\n");
            exitCode = 1;
            return false;
        }
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = writePipe;
        startup.hStdError = writePipe;
        PROCESS_INFORMATION processInfo{};
        std::wstring commandLine = BuildCommandLine(executable, arguments);

        const BOOL created = CreateProcessW(
            executable.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            workingDirectory.c_str(),
            &startup,
            &processInfo);
        CloseHandle(writePipe);
        writePipe = nullptr;

        if (!created) {
            const DWORD error = GetLastError();
            CloseHandle(readPipe);
            PostLog(L"\r\nCould not start " + executable + L": " + WindowsErrorMessage(error) + L"\r\n");
            exitCode = error;
            return false;
        }

        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))
                || !AssignProcessToJobObject(job, processInfo.hProcess)) {
                CloseHandle(job);
                job = nullptr;
            }
        }

        {
            std::lock_guard<std::mutex> lock(processMutex_);
            currentProcess_ = processInfo.hProcess;
            currentJob_ = job;
        }
        if (cancelRequested_.load()) {
            TerminateCurrentProcess();
        }

        char buffer[4096];
        for (;;) {
            DWORD bytesRead = 0;
            if (!ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) || bytesRead == 0) {
                break;
            }
            const std::string chunk(buffer, buffer + bytesRead);
            if (capturedOutput) {
                capturedOutput->append(chunk);
            }
            PostLog(Utf8ToWide(chunk));
        }
        CloseHandle(readPipe);
        WaitForSingleObject(processInfo.hProcess, INFINITE);
        GetExitCodeProcess(processInfo.hProcess, &exitCode);

        {
            std::lock_guard<std::mutex> lock(processMutex_);
            if (currentProcess_ == processInfo.hProcess) currentProcess_ = nullptr;
            if (currentJob_ == job) currentJob_ = nullptr;
        }
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        if (job) CloseHandle(job);
        return true;
    }

    void PostLog(const std::wstring& text) {
        auto* copy = new std::wstring(text);
        if (!PostMessageW(hwnd_, kAppLogMessage, 0, reinterpret_cast<LPARAM>(copy))) {
            delete copy;
        }
    }

    void PostComplete(DWORD exitCode, bool cancelled) {
        auto* result = new DownloadResult{exitCode, cancelled};
        if (!PostMessageW(hwnd_, kAppCompleteMessage, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }

    void FinishDownload(const DownloadResult& result) {
        if (downloadWorker_.joinable()) {
            downloadWorker_.join();
        }
        isRunning_ = false;
        if (result.cancelled) {
            AppendLog(L"\r\nCancelled.\r\n");
            UpdateStatus();
        } else if (result.exitCode == 0) {
            AppendLog(L"\r\nDone.\r\n");
            MessageBeep(MB_OK);
            UpdateStatus();
        } else {
            AppendLog(L"\r\nThe download did not complete.\r\n");
            UpdateStatus();
            ShowError(L"The download did not complete. Check the activity log for details.");
        }
        UpdateFooterButtons();
    }

private:
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND contentHost_ = nullptr;

    HWND sourceHeading_ = nullptr;
    HWND metadataHeading_ = nullptr;
    HWND saveHeading_ = nullptr;
    HWND activityHeading_ = nullptr;
    HWND descriptionLabel_ = nullptr;
    HWND supportedLabel_ = nullptr;
    HWND urlEdit_ = nullptr;
    HWND pasteButton_ = nullptr;
    HWND platformLabel_ = nullptr;
    std::vector<MetadataRow> metadataRows_;
    HWND moreTagsButton_ = nullptr;
    HWND explicitCheck_ = nullptr;
    HWND saveFolderLabel_ = nullptr;
    HWND outputEdit_ = nullptr;
    HWND chooseButton_ = nullptr;
    HWND logEdit_ = nullptr;

    HWND statusLabel_ = nullptr;
    HWND showDownloadsButton_ = nullptr;
    HWND downloadButton_ = nullptr;
    HWND cancelButton_ = nullptr;

    HFONT fontNormal_ = nullptr;
    HFONT fontSmall_ = nullptr;
    HFONT fontBold_ = nullptr;
    HFONT fontMono_ = nullptr;
    HFONT fontTitle_ = nullptr;
    UINT dpi_ = 96;

    std::wstring outputDirectory_;
    std::wstring logText_;
    std::wstring ytDlpPath_;
    std::wstring ffmpegPath_;
    std::wstring atomicParsleyPath_;
    std::vector<std::wstring> missingDependencies_;
    bool moreTagsExpanded_ = false;
    bool isRunning_ = false;
    int scrollOffset_ = 0;
    int contentHeight_ = 0;

    std::thread downloadWorker_;
    std::mutex processMutex_;
    HANDLE currentProcess_ = nullptr;
    HANDLE currentJob_ = nullptr;
    std::atomic_bool cancelRequested_{false};
};

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX commonControls{sizeof(commonControls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&commonControls);

    MainWindow window;
    if (!window.Create(instance, showCommand)) {
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
        MessageBoxW(nullptr, L"AM Download could not start.", L"AM Download", MB_OK | MB_ICONERROR);
        return 1;
    }

    const int result = window.Run();
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
    return result;
}
