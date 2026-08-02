#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")

namespace {

constexpr wchar_t kProductName[] = L"AM Download";
constexpr wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AM Download";

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

std::wstring ModulePath() {
    std::vector<wchar_t> buffer(260);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return L"";
        }
        if (length < buffer.size() - 1) {
            return std::wstring(buffer.data(), length);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path ModuleDirectory() {
    return std::filesystem::path(ModulePath()).parent_path();
}

std::filesystem::path KnownFolderPath(REFKNOWNFOLDERID folderId) {
    PWSTR rawPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &rawPath))) {
        return {};
    }
    std::filesystem::path result(rawPath);
    CoTaskMemFree(rawPath);
    return result;
}

bool IsFile(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    return !error && std::filesystem::is_regular_file(status);
}

bool EnsureDirectory(const std::filesystem::path& path, std::wstring& errorMessage) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        errorMessage = L"Could not create:\n\n" + path.wstring() + L"\n\n" + std::wstring(error.message().begin(), error.message().end());
        return false;
    }
    return true;
}

bool CopyDirectory(const std::filesystem::path& source, const std::filesystem::path& destination, std::wstring& errorMessage) {
    if (!std::filesystem::exists(source)) {
        return true;
    }
    if (!EnsureDirectory(destination, errorMessage)) {
        return false;
    }

    std::error_code iteratorError;
    for (const auto& entry : std::filesystem::directory_iterator(source, iteratorError)) {
        if (iteratorError) {
            errorMessage = L"Could not read:\n\n" + source.wstring();
            return false;
        }
        const auto destinationPath = destination / entry.path().filename();
        if (entry.is_directory(iteratorError)) {
            if (!CopyDirectory(entry.path(), destinationPath, errorMessage)) {
                return false;
            }
        } else if (entry.is_regular_file(iteratorError)) {
            if (!CopyFileW(entry.path().c_str(), destinationPath.c_str(), FALSE)) {
                errorMessage = L"Could not copy:\n\n" + entry.path().wstring() + L"\n\nWindows error " + std::to_wstring(GetLastError());
                return false;
            }
        }
    }
    return true;
}

bool CreateShortcut(const std::filesystem::path& shortcutPath, const std::filesystem::path& targetPath,
    const std::filesystem::path& workingDirectory, const std::wstring& description) {
    std::error_code error;
    std::filesystem::create_directories(shortcutPath.parent_path(), error);
    if (error) {
        return false;
    }

    IShellLinkW* shellLink = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
    if (FAILED(result)) {
        return false;
    }

    shellLink->SetPath(targetPath.c_str());
    shellLink->SetWorkingDirectory(workingDirectory.c_str());
    shellLink->SetDescription(description.c_str());

    IPersistFile* persistFile = nullptr;
    result = shellLink->QueryInterface(IID_PPV_ARGS(&persistFile));
    if (SUCCEEDED(result)) {
        result = persistFile->Save(shortcutPath.c_str(), TRUE);
        persistFile->Release();
    }
    shellLink->Release();
    return SUCCEEDED(result);
}

std::vector<std::filesystem::path> ShortcutPaths() {
    const auto commonPrograms = KnownFolderPath(FOLDERID_CommonPrograms);
    const auto publicDesktop = KnownFolderPath(FOLDERID_PublicDesktop);
    const auto roamingAppData = KnownFolderPath(FOLDERID_RoamingAppData);
    const auto quickLaunch = roamingAppData / L"Microsoft" / L"Internet Explorer" / L"Quick Launch";

    return {
        commonPrograms / L"AM Download.lnk",
        publicDesktop / L"AM Download.lnk",
        quickLaunch / L"AM Download.lnk"
    };
}

std::filesystem::path InstallDirectory() {
    return KnownFolderPath(FOLDERID_ProgramFiles) / kProductName;
}

bool WriteRegistry(const std::filesystem::path& installDirectory, std::wstring& errorMessage) {
    HKEY key = nullptr;
    const LONG opened = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (opened != ERROR_SUCCESS) {
        errorMessage = L"Could not register the uninstaller. Windows error " + std::to_wstring(opened);
        return false;
    }

    const std::wstring uninstallPath = (installDirectory / L"Uninstall.exe").wstring();
    const std::wstring uninstallCommand = L"\"" + uninstallPath + L"\" /uninstall";
    const std::wstring installPath = installDirectory.wstring();
    const std::wstring displayIcon = (installDirectory / L"AMDownload.exe").wstring();
    const std::wstring publisher = L"AM Download";
    const std::wstring version = L"1.0.0";
    const DWORD one = 1;

    auto writeString = [&](const wchar_t* name, const std::wstring& value) {
        return RegSetValueExW(key, name, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    };
    const bool success = writeString(L"DisplayName", kProductName)
        && writeString(L"DisplayVersion", version)
        && writeString(L"Publisher", publisher)
        && writeString(L"InstallLocation", installPath)
        && writeString(L"DisplayIcon", displayIcon)
        && writeString(L"UninstallString", uninstallCommand)
        && RegSetValueExW(key, L"NoModify", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one)) == ERROR_SUCCESS
        && RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one)) == ERROR_SUCCESS;
    RegCloseKey(key);
    if (!success) {
        errorMessage = L"Could not finish registering the uninstaller.";
    }
    return success;
}

bool Install(std::wstring& errorMessage) {
    const std::filesystem::path sourceDirectory = ModuleDirectory();
    const std::filesystem::path sourceApplication = sourceDirectory / L"AMDownload.exe";
    const std::filesystem::path sourceInstaller = ModulePath();
    const std::filesystem::path targetDirectory = InstallDirectory();
    const std::filesystem::path targetApplication = targetDirectory / L"AMDownload.exe";
    const std::filesystem::path targetUninstaller = targetDirectory / L"Uninstall.exe";

    if (!IsFile(sourceApplication)) {
        errorMessage = L"The installer payload is incomplete. AMDownload.exe was not found next to the installer.";
        return false;
    }
    if (!EnsureDirectory(targetDirectory, errorMessage)) {
        return false;
    }
    if (!CopyFileW(sourceApplication.c_str(), targetApplication.c_str(), FALSE)) {
        errorMessage = L"Could not install AMDownload.exe. Windows error " + std::to_wstring(GetLastError());
        return false;
    }

    const auto sourceTools = sourceDirectory / L"tools";
    if (!CopyDirectory(sourceTools, targetDirectory / L"tools", errorMessage)) {
        return false;
    }
    if (!CopyFileW(sourceInstaller.c_str(), targetUninstaller.c_str(), FALSE)) {
        errorMessage = L"Could not install the uninstaller. Windows error " + std::to_wstring(GetLastError());
        return false;
    }

    for (const auto& shortcut : ShortcutPaths()) {
        if (!CreateShortcut(shortcut, targetApplication, targetDirectory, L"Download tagged M4A files for Apple Music")) {
            errorMessage = L"Could not create shortcut:\n\n" + shortcut.wstring();
            return false;
        }
    }
    return WriteRegistry(targetDirectory, errorMessage);
}

bool Uninstall(std::wstring& errorMessage) {
    const std::filesystem::path targetDirectory = InstallDirectory();
    for (const auto& shortcut : ShortcutPaths()) {
        DeleteFileW(shortcut.c_str());
    }
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kUninstallKey);

    std::error_code error;
    const auto currentModule = std::filesystem::path(ModulePath());
    for (const auto& entry : std::filesystem::directory_iterator(targetDirectory, error)) {
        if (error) break;
        if (entry.path() == currentModule) continue;
        std::filesystem::remove_all(entry.path(), error);
        if (error) {
            error.clear();
        }
    }
    MoveFileExW(currentModule.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    std::filesystem::remove(targetDirectory, error);
    if (error && error.value() != ERROR_DIR_NOT_EMPTY) {
        errorMessage = L"Some installer files will be removed after the next restart.";
    }
    return true;
}

bool HasArgument(int argc, LPWSTR* argv, const wchar_t* expected) {
    for (int index = 1; index < argc; ++index) {
        if (Lowercase(argv[index]) == Lowercase(expected)) {
            return true;
        }
    }
    return false;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool uninstall = argv && HasArgument(argc, argv, L"/uninstall");
    const bool quiet = argv && HasArgument(argc, argv, L"/quiet");
    if (argv) {
        LocalFree(argv);
    }

    std::wstring errorMessage;
    const bool success = uninstall ? Uninstall(errorMessage) : Install(errorMessage);
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }

    if (!success) {
        MessageBoxW(nullptr, errorMessage.c_str(), L"AM Download Setup", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!quiet) {
        const wchar_t* message = uninstall
            ? L"AM Download was removed from this system."
            : L"AM Download was installed. Start Menu, desktop, and Quick Launch shortcuts are ready.";
        MessageBoxW(nullptr, message, L"AM Download Setup", MB_OK | MB_ICONINFORMATION);
    }
    return 0;
}
