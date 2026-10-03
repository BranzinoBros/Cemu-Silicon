#include "IAudioAPI.h"

#include "config/CemuConfig.h"
#if HAS_CUBEB
#include "CubebAPI.h"
#endif

std::shared_mutex g_audioMutex;
AudioAPIPtr g_tvAudio;
AudioAPIPtr g_padAudio;
AudioAPIPtr g_portalAudio;
std::atomic_int32_t g_padVolume = 0;

uint32 IAudioAPI::s_audioDelay = 2;
std::array<bool, IAudioAPI::AudioAPIEnd> IAudioAPI::s_availableApis{};

IAudioAPI::IAudioAPI(uint32 samplerate, uint32 channels, uint32 samples_per_block, uint32 bits_per_sample)
	: m_samplerate(samplerate), m_channels(channels), m_samplesPerBlock(samples_per_block), m_bitsPerSample(bits_per_sample)
{
	m_bytesPerBlock = samples_per_block * channels * (bits_per_sample / 8);
}

void IAudioAPI::PrintLogging()
{
	cemuLog_log(LogType::Force, "------- Init Audio backend -------");
	cemuLog_log(LogType::Force, "Cubeb: {}", s_availableApis[Cubeb] ? "available" : "not supported");
}

void IAudioAPI::InitializeStatic()
{
	s_audioDelay = GetConfig().audio_delay;

#if HAS_CUBEB
	s_availableApis[Cubeb] = CubebAPI::InitializeStatic();
#endif
}

bool IAudioAPI::IsAudioAPIAvailable(AudioAPI api)
{
	if ((size_t)api < s_availableApis.size())
		return s_availableApis[api];

	cemu_assert_debug(false);
	return false;
}

AudioAPIPtr IAudioAPI::CreateDeviceFromConfig(AudioType type, sint32 rate, sint32 samples_per_block, sint32 bits_per_sample)
{
	sint32 channels = CemuConfig::AudioChannelsToNChannels(AudioTypeToChannels(type));
	return CreateDeviceFromConfig(type, rate, channels, samples_per_block, bits_per_sample);
}

AudioAPIPtr IAudioAPI::CreateDeviceFromConfig(AudioType type, sint32 rate, sint32 channels, sint32 samples_per_block, sint32 bits_per_sample)
{
	AudioAPIPtr audioAPIDev;

	auto& config = GetConfig();

	const auto audio_api = (IAudioAPI::AudioAPI)config.audio_api;
	auto selectedDevice = GetDeviceFromType(type);

	if (selectedDevice.empty())
		return {};

	IAudioAPI::DeviceDescriptionPtr device_description;
	if (IAudioAPI::IsAudioAPIAvailable(audio_api))
	{
		auto devices = IAudioAPI::GetDevices(audio_api);
		const auto it = std::find_if(devices.begin(), devices.end(), [&selectedDevice](const auto& d) { return d->GetIdentifier() == selectedDevice; });
		if (it != devices.end())
			device_description = *it;
	}
	if (!device_description)
		throw std::runtime_error("failed to find selected device while trying to create audio device");

	audioAPIDev = CreateDevice(audio_api, device_description, rate, channels, samples_per_block, bits_per_sample);
	audioAPIDev->SetVolume(GetVolumeFromType(type));

	return audioAPIDev;
}

AudioAPIPtr IAudioAPI::CreateDevice(AudioAPI api, const DeviceDescriptionPtr& device, sint32 samplerate, sint32 channels, sint32 samples_per_block, sint32 bits_per_sample)
{
	if (!IsAudioAPIAvailable(api))
		return {};

	switch (api)
	{
#if HAS_CUBEB
	case Cubeb:
	{
		const auto tmp = std::dynamic_pointer_cast<CubebAPI::CubebDeviceDescription>(device);
		return std::make_unique<CubebAPI>(tmp->GetDeviceId(), samplerate, channels, samples_per_block, bits_per_sample);
	}
#endif
	default:
		throw std::runtime_error(fmt::format("invalid audio api: {}", api));
	}
}

std::vector<IAudioAPI::DeviceDescriptionPtr> IAudioAPI::GetDevices(AudioAPI api)
{
	if (!IsAudioAPIAvailable(api))
		return {};

	switch (api)
	{
#if HAS_CUBEB
	case Cubeb:
	{
		return CubebAPI::GetDevices();
	}
#endif
	default:
		throw std::runtime_error(fmt::format("invalid audio api: {}", api));
	}
}

void IAudioAPI::SetAudioDelayOverride(uint32 delay)
{
	m_audioDelayOverride = delay;
}

uint32 IAudioAPI::GetAudioDelay() const
{
	return m_audioDelayOverride > 0 ? m_audioDelayOverride : s_audioDelay;
}

AudioChannels IAudioAPI::AudioTypeToChannels(AudioType type)
{
	auto& config = GetConfig();
	switch (type)
	{
	case TV:
		return config.tv_channels;
	case Gamepad:
		return config.pad_channels;
	case Portal:
		return kMono;
	default:
		return kMono;
	}
}

std::wstring IAudioAPI::GetDeviceFromType(AudioType type)
{
	auto& config = GetConfig();
	switch (type)
	{
	case TV:
		return config.tv_device;
	case Gamepad:
		return config.pad_device;
	case Portal:
		return config.portal_device;
	default:
		return L"";
	}
}

sint32 IAudioAPI::GetVolumeFromType(AudioType type)
{
	auto& config = GetConfig();
	switch (type)
	{
	case TV:
		return config.tv_volume;
	case Gamepad:
		return config.pad_volume;
	case Portal:
		return config.portal_volume;
	default:
		return 0;
	}
}
