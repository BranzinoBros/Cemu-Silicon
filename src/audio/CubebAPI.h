#pragma once

#include "IAudioAPI.h"

#include <cubeb/cubeb.h>

#include <atomic>
#include <memory>
#include <mutex>

class CubebAPI : public IAudioAPI
{
public:
	class CubebDeviceDescription : public DeviceDescription
	{
	public:
		CubebDeviceDescription(cubeb_devid devid, std::string device_id, const std::wstring& name)
			: DeviceDescription(name), m_devid(devid), m_device_id(std::move(device_id)) { }

		std::wstring GetIdentifier() const override { return  boost::nowide::widen(m_device_id); }
		cubeb_devid GetDeviceId() const { return m_devid; }

	private:
		cubeb_devid m_devid;
		std::string m_device_id;
	};

	using CubebDeviceDescriptionPtr = std::shared_ptr<CubebDeviceDescription>;

	CubebAPI(cubeb_devid devid, uint32 samplerate, uint32 channels, uint32 samples_per_block, uint32 bits_per_sample);
	~CubebAPI();

	AudioAPI GetType() const override { return Cubeb; }
	bool NeedAdditionalBlocks() const override;
	bool FeedBlock(sint16* data) override;
	bool Play() override;
	bool Stop() override;
	void SetVolume(sint32 volume) override;

	static std::vector<DeviceDescriptionPtr> GetDevices();

	static bool InitializeStatic();
	static void Destroy();

private:
	inline static cubeb* s_context = nullptr;

	cubeb_stream* m_stream = nullptr;
	bool m_is_playing = false;

	// single-producer/single-consumer ring buffer between FeedBlock() and the realtime data_cb()
	// data_cb() never blocks, m_producerMutex only serializes FeedBlock() callers
	std::unique_ptr<uint8[]> m_buffer;
	size_t m_bufferSize = 0;
	std::atomic<size_t> m_readPos{ 0 }; // total bytes consumed, only advanced by data_cb()
	std::atomic<size_t> m_writePos{ 0 }; // total bytes queued, only advanced by FeedBlock()
	std::mutex m_producerMutex;
	static_assert(std::atomic<size_t>::is_always_lock_free);

	static long data_cb(cubeb_stream* stream, void* user, const void* inputbuffer, void* outputbuffer, long nframes);
};
