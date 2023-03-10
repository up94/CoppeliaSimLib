#include <simInternal.h>
#include <folderSystem.h>
#include <vFile.h>
#include <global.h>
#include <vVarious.h>
#include <app.h>
#include <simFlavor.h>
#ifdef __cpp_lib_filesystem // macOS 10.13 does not support XCode >=11 which is required for that
    #include <filesystem>
#else
    #include <ftw.h>
    #include <unistd.h>
#endif
#ifdef SIM_WITH_QT
    #include <QStandardPaths>
#endif

CFolderSystem::CFolderSystem()
{
    _executablePath=VVarious::getModulePath();
    VVarious::removePathFinalSlashOrBackslash(_executablePath);
#ifdef MAC_SIM
    _resourcesPath=_executablePath+"/../Resources";
#else
    _resourcesPath=_executablePath;
#endif
    _systemPath=_resourcesPath+"/system";
    _scenesPath=_resourcesPath+"/scenes";
    _modelsPath=_resourcesPath+"/models";
    _importExportPath=_resourcesPath+"/cadFiles";
    _addOnPath=_resourcesPath+"/addOns";
    _videosPath=_resourcesPath;
    _otherFilesPath=_resourcesPath;
    _scenesDefaultPath=_scenesPath;
    _modelsDefaultPath=_modelsPath;
    if (App::userSettings->defaultDirectoryForScenes.length()!=0)
        setScenesPath(App::userSettings->defaultDirectoryForScenes.c_str());
    if (App::userSettings->defaultDirectoryForModels.length()!=0)
        setModelsPath(App::userSettings->defaultDirectoryForModels.c_str());
    if (App::userSettings->defaultDirectoryForImportExport.length()!=0)
        setImportExportPath(App::userSettings->defaultDirectoryForImportExport.c_str());
    if (App::userSettings->defaultDirectoryForMiscFiles.length()!=0)
        setOtherFilesPath(App::userSettings->defaultDirectoryForMiscFiles.c_str());

#ifdef SIM_WITH_QT
    _tempDir=new QTemporaryDir();
    _tempDir->setAutoRemove(true);
    if (_tempDir->isValid())
        _tempDataPath=_tempDir->path().toStdString().c_str();
    QString s(QStandardPaths::locate(QStandardPaths::AppDataLocation,"CoppeliaSim",QStandardPaths::LocateDirectory));
    if (s.length()==0)
    {
        QString wl(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
        QDir(wl).mkdir("CoppeliaSim");
    }
    _appDataPath=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString()+"/CoppeliaSim";
#endif
    if (_tempDataPath.size()==0)
    {
        _tempDataPath=_executablePath+"/tmp";
        if (!VFile::doesFolderExist(_tempDataPath.c_str()))
            VFile::createFolder(_tempDataPath.c_str());
    }
    if (_appDataPath.size()==0)
    {
        _appDataPath=_executablePath+"/app";
        if (!VFile::doesFolderExist(_appDataPath.c_str()))
            VFile::createFolder(_appDataPath.c_str());
    }
}

#ifndef  __cpp_lib_filesystem // macOS 10.13 does not support XCode >=11 which is required for that
int unlinkCb(const char* fpath,const struct stat* sb,int typeflag,struct FTW* ftwbuf)
{
    int rv=remove(fpath);
    return rv;
}

int rmrf(const char* path)
{
    return nftw(path,unlinkCb,64,FTW_DEPTH|FTW_PHYS);
}
#endif

CFolderSystem::~CFolderSystem()
{
#ifdef SIM_WITH_QT
    if (_tempDir->isValid())
        delete _tempDir;
        _tempDataPath.clear();
#endif
    if (_tempDataPath.size()>0)
    {
#ifdef __cpp_lib_filesystem // macOS 10.13 does not support XCode >=11 which is required for that
        try
        {
            std::filesystem::remove_all(_tempDataPath.c_str());
        }
        catch(std::filesystem::filesystem_error const &e)
        {
        }
#else
        rmrf(_tempDataPath.c_str());
#endif
    }
}

std::string CFolderSystem::getPathFromFull(const char* full)
{ // no final slash is returned
    return(VVarious::splitPath_path(full));
}

std::string CFolderSystem::getNameFromFull(const char* full)
{
    return(VVarious::splitPath_fileBase(full));
}

std::string CFolderSystem::getExecutablePath() const
{
    return(_executablePath);
}

std::string CFolderSystem::getAddOnPath() const
{
    return(_addOnPath);
}

std::string CFolderSystem::getInterpretersRootPath() const
{
    return(_resourcesPath);
}

std::string CFolderSystem::getSystemPath() const
{
    return(_systemPath);
}

std::string CFolderSystem::getAutoSavedScenesPath() const
{
    return(getUserSettingsPath()+"/autoSavedScenes");
}

std::string CFolderSystem::getResourcesPath() const
{
    return(_resourcesPath);
}

std::string CFolderSystem::getScenesDefaultPath() const
{
    return(_scenesDefaultPath);
}

std::string CFolderSystem::getModelsDefaultPath() const
{
    return(_modelsDefaultPath);
}

std::string CFolderSystem::getScenesPath() const
{
    return(_scenesPath);
}

void CFolderSystem::setScenesPath(const char* path)
{
    _scenesPath=path;
    VVarious::removePathFinalSlashOrBackslash(_scenesPath);
}

std::string CFolderSystem::getModelsPath() const
{
    return(_modelsPath);
}

void CFolderSystem::setModelsPath(const char* path)
{
    _modelsPath=path;
    VVarious::removePathFinalSlashOrBackslash(_modelsPath);
}

std::string CFolderSystem::getImportExportPath() const
{
    return(_importExportPath);
}

void CFolderSystem::setImportExportPath(const char* path)
{
    _importExportPath=path;
    VVarious::removePathFinalSlashOrBackslash(_importExportPath);
}

std::string CFolderSystem::getTexturesPath() const
{
    return(_texturesPath);
}

std::string CFolderSystem::getLuaPath() const
{
    return(_resourcesPath+"/lua");
}

std::string CFolderSystem::getPythonPath() const
{
    return(_resourcesPath+"/python");
}

std::string CFolderSystem::getMujocoPath() const
{
    return(_resourcesPath+"/mujoco");
}

std::string CFolderSystem::getUserSettingsPath()
{ // static since needed before construction
    static std::string userSettingsFolder;
    if (userSettingsFolder.size()==0)
    {
        std::string usrSet("CoppeliaSim");
        char *ps=std::getenv("COPPELIASIM_USER_SETTINGS_FOLDER_SUFFIX");
        if (ps!=nullptr)
            usrSet+=ps;
        const char* home=std::getenv("HOME");
    #ifdef WIN_SIM
        const char* appData=std::getenv("appdata");
        if (appData!=nullptr)
            userSettingsFolder=std::string(appData)+"/"+usrSet;
    #endif
    #ifdef LIN_SIM
        const char* xdghome=std::getenv("XDG_CONFIG_HOME");
        if (xdghome!=nullptr)
            xdghome=home;
        if (xdghome!=nullptr)
            userSettingsFolder=std::string(xdghome)+"/."+usrSet;
    #endif
    #ifdef MAC_SIM
        if (home!=nullptr)
            userSettingsFolder=std::string(home)+"/."+usrSet;
    #endif
        if (userSettingsFolder.size()==0)
        { // fallback
#ifdef MAC_SIM
            userSettingsFolder=VVarious::getModulePath()+"/../Resources/system";
#else
            userSettingsFolder=VVarious::getModulePath()+"/system";
#endif
        }
    }
    return(userSettingsFolder);
}

void CFolderSystem::setTexturesPath(const char* path)
{
    _texturesPath=path;
    VVarious::removePathFinalSlashOrBackslash(_texturesPath);
}

std::string CFolderSystem::getVideosPath() const
{
    return(_videosPath);
}

void CFolderSystem::setVideosPath(const char* path)
{
    _videosPath=path;
    VVarious::removePathFinalSlashOrBackslash(_videosPath);
}

std::string CFolderSystem::getOtherFilesPath() const
{
    return(_otherFilesPath);
}

void CFolderSystem::setOtherFilesPath(const char* path)
{
    _otherFilesPath=path;
    VVarious::removePathFinalSlashOrBackslash(_otherFilesPath);
}

std::string CFolderSystem::getAppDataPath() const
{
    return(_appDataPath);
}

std::string CFolderSystem::getTempDataPath() const
{
    return(_tempDataPath);
}

std::string CFolderSystem::getSceneTempDataPath() const
{
    std::string folder=_tempDataPath+"/sceneData"+std::to_string(App::currentWorld->environment->getSceneUniqueID());
    if (!VFile::doesFolderExist(folder.c_str()))
        VFile::createFolder(folder.c_str());
    return(folder);
}
