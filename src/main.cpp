#include "WindowSystem.h"
#include "util/crypto/aes128.h"
#include "Cafe/OS/RPL/rpl.h"
#include "Cafe/OS/libs/gx2/GX2.h"
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"
#include "Cafe/GameProfile/GameProfile.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "config/CemuConfig.h"
#include "config/NetworkSettings.h"
#include "config/LaunchSettings.h"
#include "input/InputManager.h"

#include "Cafe/CafeSystem.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cafe/TitleList/SaveList.h"

#include "Common/ExceptionHandler/ExceptionHandler.h"
#include "Common/cpu_features.h"

#include "util/helpers/helpers.h"
#include "config/ActiveSettings.h"

#include "Cafe/IOSU/legacy/iosu_crypto.h"
#include "Cafe/OS/libs/vpad/vpad.h"

#include "audio/IAudioAPI.h"
#include "audio/IAudioInputAPI.h"

#ifdef HAS_SDL
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#endif

#define _putenv(__s) putenv((char*)(__s))
#include <sys/types.h>
#include <sys/sysctl.h>

std::atomic_bool g_isGPUInitFinished = false;

std::wstring executablePath;

// some implementations of _putenv dont copy the string and instead only store a pointer
// thus we use a helper to keep a permanent copy
std::vector<std::string*> sPutEnvMap;

void _putenvSafe(const char* c)
{
    auto s = new std::string(c);
    sPutEnvMap.emplace_back(s);
    _putenv(s->c_str());
}

void reconfigureGLDrivers()
{
#ifdef ENABLE_OPENGL
	// reconfigure GL drivers to store
	const fs::path nvCacheDir = ActiveSettings::GetCachePath("shaderCache/driver/nvidia/");

	std::error_code err;
	fs::create_directories(nvCacheDir, err);

	std::string nvCacheDirEnvOption("__GL_SHADER_DISK_CACHE_PATH=");
	nvCacheDirEnvOption.append(_pathToUtf8(nvCacheDir));

    _putenvSafe(nvCacheDirEnvOption.c_str());
    _putenvSafe("__GL_SHADER_DISK_CACHE_SKIP_CLEANUP=1");
#endif
}

void reconfigureVkDrivers()
{
#ifdef ENABLE_VULKAN
    _putenvSafe("DISABLE_LAYER_AMD_SWITCHABLE_GRAPHICS_1=1");
    _putenvSafe("DISABLE_VK_LAYER_VALVE_steam_fossilize_1=1");
#endif
}

void CemuCommonInit()
{
	reconfigureGLDrivers();
	reconfigureVkDrivers();
	// crypto init
	AES128_init();
	// init PPC timer
	// call this as early as possible because it measures frequency of RDTSC using an asynchronous thread over 3 seconds
	PPCTimer_init();

    ExceptionHandler_Init();
	// read config
	GetConfigHandle().Load();
	if (NetworkConfig::XMLExists())
		n_config.Load();
	// parallelize expensive init code
	std::future<int> futureInitAudioAPI = std::async(std::launch::async, []{ IAudioAPI::InitializeStatic(); IAudioInputAPI::InitializeStatic(); return 0; });
	std::future<int> futureInitGraphicPacks = std::async(std::launch::async, []{ GraphicPack2::LoadAll(); return 0; });
	InputManager::instance().load();
	futureInitAudioAPI.wait();
	futureInitGraphicPacks.wait();
	// init Cafe system
	CafeSystem::Initialize();
	// init title list
	CafeTitleList::Initialize(ActiveSettings::GetUserDataPath("title_list_cache.xml"));
	for (auto& it : GetConfig().game_paths)
		CafeTitleList::AddScanPath(_utf8ToPath(it));
	fs::path mlcPath = ActiveSettings::GetMlcPath();
	if (!mlcPath.empty())
		CafeTitleList::SetMLCPath(mlcPath);
	CafeTitleList::Refresh();
	// init save list
	CafeSaveList::Initialize();
	if (!mlcPath.empty())
	{
		CafeSaveList::SetMLCPath(mlcPath);
		CafeSaveList::Refresh();
	}
}

void mainEmulatorLLE();
void ppcAsmTest();
void gx2CopySurfaceTest();
void ExpressionParser_test();
void FSTVolumeTest();
void CRCTest();

void UnitTests()
{
	ExpressionParser_test();
	gx2CopySurfaceTest();
	ppcAsmTest();
	FSTVolumeTest();
	CRCTest();
}

void HandlePostUpdate()
{
	auto exeBackupPath = ActiveSettings::GetExecutablePath();
	exeBackupPath.replace_extension( _utf8ToPath(_pathToUtf8(exeBackupPath.extension()).append(".backup")));
	std::error_code ec;
	if (!fs::exists(exeBackupPath, ec))
		return;
	// try to delete update residue, but give up quickly as to not cause a permanent hang
	// it may succeed on next turn
	for (sint32 i=0; i<3; i++)
	{
		if (fs::remove(exeBackupPath, ec))
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
}

void ToolShaderCacheMerger();

int main(int argc, char *argv[])
{
	auto parse_rc = LaunchSettings::HandleCommandline(argc, argv);
  if (parse_rc.has_value())
		return *parse_rc;
	WindowSystem::Create();
	return 0;
}

extern "C" DLLEXPORT uint64 gameMeta_getTitleId()
{
	return CafeSystem::GetForegroundTitleId();
}
