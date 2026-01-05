#include "pch.h"
#include "Helpers.h"
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>
#include <filesystem>

std::filesystem::path Helpers::GetModelPath(std::string folderName) {
    winrt::Windows::Storage::StorageFolder packageFolder = winrt::Windows::ApplicationModel::Package::Current().InstalledLocation();
    std::filesystem::path packagePath = packageFolder.Path().c_str();
    std::filesystem::path fullModelPath = packagePath / folderName;
    return fullModelPath;
}

