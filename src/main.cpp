#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <chrono>
#include <ctime>
#include <map>
#include <windows.h>

#include "toml11/toml.hpp"

std::chrono::system_clock::time_point fileLastWriteTime(const std::wstring& filePath) {
    HANDLE hFile = CreateFileW(
        filePath.c_str(),          // File path
        GENERIC_READ,              // Access mode
        FILE_SHARE_READ,           // Share mode
        NULL,                      // Security attributes
        OPEN_EXISTING,             // Open the file if it exists
        FILE_ATTRIBUTE_NORMAL,     // File attributes
        NULL);                     // Template file

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Error opening file: " << GetLastError() << std::endl;
        return std::chrono::system_clock::time_point::min();  // Return a minimum time point
    }

    FILETIME creationTime, lastAccessTime, lastWriteTime;
    if (GetFileTime(hFile, &creationTime, &lastAccessTime, &lastWriteTime)) {
        // Convert FILETIME to SYSTEMTIME
        SYSTEMTIME lastWriteSysTime;
        FileTimeToSystemTime(&lastWriteTime, &lastWriteSysTime);

        // Convert SYSTEMTIME to system_clock::time_point
        std::tm tm = {};
        tm.tm_year = lastWriteSysTime.wYear - 1900;
        tm.tm_mon = lastWriteSysTime.wMonth - 1;
        tm.tm_mday = lastWriteSysTime.wDay;
        tm.tm_hour = lastWriteSysTime.wHour;
        tm.tm_min = lastWriteSysTime.wMinute;
        tm.tm_sec = lastWriteSysTime.wSecond;

        std::time_t time = std::mktime(&tm);
        CloseHandle(hFile);
        return std::chrono::system_clock::from_time_t(time);
    } else {
        std::cerr << "Error getting file times: " << GetLastError() << std::endl;
        CloseHandle(hFile);
        return std::chrono::system_clock::time_point::min();  // Return a minimum time point
    }
}

int main(int argc, char *argv[])
{
    std::filesystem::path currentPath = std::filesystem::current_path();
    std::filesystem::path buildFile;

    for (const auto& entry : std::filesystem::directory_iterator(currentPath))
    {
        if (!entry.is_directory()  && entry.path().filename().string() == "build.toml")
        {
            buildFile = entry.path();
            break;
        }
    }

    if (buildFile.empty())
    {
        std::cout << "No build.toml found" << std::endl;
        return EXIT_FAILURE;
    }

    if (argc > 1)
    {
        if (std::string(argv[1]) != "debug" && std::string(argv[1]) != "release")
        {
            std::cout <<"Invalad arg: " << argv[1] << std::endl;
            return EXIT_FAILURE;
        }
    }

    auto data = toml::parse(buildFile.string(), toml::spec::v(1,1,0));

    std::string name = toml::find<std::string>(data, "project", "name");
    std::string type = toml::find<std::string>(data, "project", "type");

    std::string cc = toml::find<std::string>(data, "compiler", "cc");
    std::vector<std::string> ldflags = toml::find<std::vector<std::string>>(data, "compiler", "ldflags");
    std::vector<std::string> libs = toml::find<std::vector<std::string>>(data, "compiler", "libs");

    std::string binPath = toml::find<std::string>(data, "paths", "bin");
    std::string objPath = toml::find<std::string>(data, "paths", "obj");

    std::vector<std::string> cdefs;
    std::vector<std::string> cflags;

    std::string arg = argv[1];
    if (arg == "release")
    {
        cdefs = toml::find<std::vector<std::string>>(data, "compiler", "release", "cdefs");
        cflags = toml::find<std::vector<std::string>>(data, "compiler", "release", "cflags");
        binPath += "/RELEASE";
        objPath += "/RELEASE";
    }
    else
    {
        cdefs = toml::find<std::vector<std::string>>(data, "compiler", "debug", "cdefs");
        cflags = toml::find<std::vector<std::string>>(data, "compiler", "debug", "cflags");
        binPath += "/DEBUG";
        objPath += "/DEBUG";
    }

    std::string srcPath = toml::find<std::string>(data, "paths", "src");
    std::string includePath = toml::find<std::string>(data, "paths", "include");
    std::string libPath = toml::find<std::string>(data, "paths", "lib");

    if (!std::filesystem::exists(srcPath))
    {
        std::cout << "[src] " << srcPath << " directory not found" << std::endl;
        return EXIT_FAILURE;
    }

    if (!std::filesystem::exists(includePath))
    {
        std::cout << "[include] " << includePath << " directory not found" << std::endl;
        return EXIT_FAILURE;
    }

    if (!std::filesystem::exists(libPath))
    {
        std::cout << "[lib] " << libPath << " directory not found" << std::endl;
        return EXIT_FAILURE;
    }

    if (!std::filesystem::exists(binPath))
    {
        std::cout << "[bin] " << binPath << " directory not found" << std::endl;
        std::filesystem::create_directories(binPath);
        std::cout << "bin path created" << std::endl;
    }

    std::map<std::string, std::chrono::_V2::system_clock::time_point> objTime;

    if (!std::filesystem::exists(objPath))
    {
        std::cout << "[obj] " << objPath << " directory not found" << std::endl;
        std::filesystem::create_directories(objPath);
        std::cout << "obj path created" << std::endl;
    }
    else // get when obj files where edited
    {
        for (const auto& entry : std::filesystem::directory_iterator(objPath))
        {
            if (!entry.is_directory() && entry.path().extension() == ".o")
            {
                objTime[entry.path().stem().string()] = fileLastWriteTime(entry.path());
            }
        }
    }

    std::string main = binPath + "/" + name + (type == "executable" ? ".exe" : ".dll");

    // get cpp files and obj files and lib files
    std::vector<std::string> cppFiles;
    std::vector<std::string> objFiles;
    std::vector<std::string> libFiles;

    std::map<std::string, std::chrono::_V2::system_clock::time_point> srcTime;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(srcPath))
    {
        if (!entry.is_directory() && entry.path().extension() == ".cpp")
        {
            cppFiles.push_back(srcPath + "/" + entry.path().filename().string());
            objFiles.push_back(objPath + "/" + entry.path().stem().string() + ".o");

            srcTime[entry.path().stem().string()] = fileLastWriteTime(entry.path());
        }
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(libPath))
        if (!entry.is_directory() && entry.path().extension() == ".a")
            libFiles.push_back(libPath + "/" + entry.path().filename().string());

    std::vector<std::string> filesToRecompile;
    std::vector<std::string> filesToRecompile2;

    if (objTime.size())
    {
        for (const auto& [fileName, objTimePoint] : objTime)
        {
            auto cppTimePointIt = srcTime.find(fileName);

            if (cppTimePointIt != srcTime.end())
            {
                auto cppTimePoint = cppTimePointIt->second;
                if (cppTimePoint > objTimePoint)
                {
                    filesToRecompile.push_back(srcPath + "/" + fileName + ".cpp");
                    filesToRecompile2.push_back(objPath + "/" + fileName + ".o");
                }
            }
        }
    }

    bool anyFilesBuilt = false;
    std::cout << "Starting build " << argv[1] << "..." << std::endl;
    auto buildStart = std::chrono::high_resolution_clock::now();

    if (objTime.size())
    {
        for (size_t i = 0; i < filesToRecompile2.size(); i++)
        {
            std::string command = cc;

            for (const auto& cflag : cflags)
                command.append(" " + cflag);

            command.append(" -c -o " + filesToRecompile2[i] + " " + filesToRecompile[i]);

            for (const auto& cdef : cdefs)
                command.append(" " + cdef);

            command.append(" -I" + includePath);

            auto start = std::chrono::high_resolution_clock::now();
            int result = std::system(command.c_str());
            auto end = std::chrono::high_resolution_clock::now();

            std::chrono::duration<double> duration = end - start;

            anyFilesBuilt = true;

            if (result == EXIT_SUCCESS)
                std::cout << objFiles[i] << " rebuilt in " << duration.count() << "s" << std::endl;
            else
                std::cout << objFiles[i] << " Failed." << std::endl;

        }
    }
    else
    {
        for (size_t i = 0; i < objFiles.size(); i++)
        {
            std::string command = cc;

            for (const auto& cflag : cflags)
                command.append(" " + cflag);

            command.append(" -c -o " + objFiles[i] + " " + cppFiles[i]);

            for (const auto& cdef : cdefs)
                command.append(" " + cdef);

            command.append(" -I" + includePath);

            auto start = std::chrono::high_resolution_clock::now();
            int result = std::system(command.c_str());
            auto end = std::chrono::high_resolution_clock::now();

            std::chrono::duration<double> duration = end - start;

            anyFilesBuilt = true;

            if (result == EXIT_SUCCESS)
                std::cout << objFiles[i] << " built in " << duration.count() << "s" << std::endl;
            else
                std::cout << objFiles[i] << " Failed." << std::endl;

        }
    }

    if (anyFilesBuilt)
    {
        std::string command = cc;

        for (const auto& cflag : cflags)
            command.append(" " + cflag);

        command.append(" -o " + main);

        for (const auto& obj : objFiles)
            command.append(" " + obj);

        command.append(" -I" + includePath);
        command.append(" -L" + libPath);

        for (const auto& lib : libFiles)
            command.append(" " + lib);

        for (const auto& cdef : cdefs)
            command.append(" " + cdef);

        for (const auto& lib : libs)
            command.append(" " + lib);

        auto start = std::chrono::high_resolution_clock::now();
        int result = std::system(command.c_str());
        auto end = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> duration = end - start;

        if (result == EXIT_SUCCESS)
            std::cout << main << " built in " << duration.count() << "s" << std::endl;
        else
            std::cout << main << " Failed." << std::endl;

        auto buildEnd = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> buildDuration = buildEnd - buildStart;
    
        std::cout << "Done in " << buildDuration.count() << "s" << std::endl;
    }
    else
    {
        std::cout << "No new changes detected" << std::endl;
    }
    
    return EXIT_SUCCESS;
}