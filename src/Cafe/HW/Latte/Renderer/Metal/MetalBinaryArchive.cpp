#include "Cafe/HW/Latte/Renderer/Metal/MetalBinaryArchive.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalCommon.h"

#include "Cemu/Logging/CemuLogging.h"
#include "config/ActiveSettings.h"
#include "util/helpers/helpers.h"

#include <pthread.h>
#include <sys/sysctl.h>

// Bump this to discard all existing archives
#define METAL_BINARY_ARCHIVE_VERSION 1
// Archives above this size are rebuilt from scratch to get rid of binaries of pipelines that are no longer used
#define METAL_BINARY_ARCHIVE_MAX_SIZE (1024ull * 1024ull * 1024ull)

// Minimum time between two serializations while a game is running
constexpr auto SERIALIZE_INTERVAL = std::chrono::seconds(60);
// Maximum time spent adding queued pipelines on title exit, whatever is left after that is dropped
constexpr auto CLOSE_FLUSH_TIMEOUT = std::chrono::seconds(5);

static NS::URL* NewFileURL(const fs::path& path)
{
    NS_STACK_SCOPED NS::String* pathStr = NS::String::alloc()->init(_pathToUtf8(path).c_str(), NS::UTF8StringEncoding);
    return NS::URL::alloc()->initFileURLWithPath(pathStr);
}

// The GPU binaries depend on the GPU and on the compiler that ships with macOS
static std::string GetArchiveKey(MTL::Device* device)
{
    char osBuild[64] = {};
    size_t size = sizeof(osBuild) - 1;
    if (sysctlbyname("kern.osversion", osBuild, &size, nullptr, 0) != 0)
        strcpy(osBuild, "unknown");

    uint32 gpuFamily = 0;
    for (uint32 family = (uint32)MTL::GPUFamilyApple9; family >= (uint32)MTL::GPUFamilyApple1; family--)
    {
        if (device->supportsFamily((MTL::GPUFamily)family))
        {
            gpuFamily = family;
            break;
        }
    }

    return fmt::format("{} {} {} {}", METAL_BINARY_ARCHIVE_VERSION, device->name()->utf8String(), gpuFamily, osBuild);
}

static std::string ReadKeyFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file)
        return {};

    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static void WriteKeyFile(const fs::path& path, const std::string& key)
{
    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (file)
        file.write(key.data(), key.size());
}

// Must be called inside an autorelease pool
static MTL::BinaryArchive* CreateArchive(MTL::Device* device, const fs::path* path)
{
    NS_STACK_SCOPED MTL::BinaryArchiveDescriptor* desc = MTL::BinaryArchiveDescriptor::alloc()->init();
    if (path)
    {
        NS_STACK_SCOPED NS::URL* url = NewFileURL(*path);
        desc->setUrl(url);
    }

    NS::Error* error = nullptr;
    MTL::BinaryArchive* archive = device->newBinaryArchive(desc, &error);
    if (!archive || error)
    {
        cemuLog_log(LogType::Force, "Failed to create Metal binary archive: {}", error ? error->localizedDescription()->utf8String() : "unknown error");
        if (archive)
            archive->release();
        return nullptr;
    }

    return archive;
}

MetalBinaryArchive::~MetalBinaryArchive()
{
    Close();
}

void MetalBinaryArchive::Open(MTL::Device* device, uint64 cacheTitleId)
{
    Close();

    std::error_code ec;
    fs::create_directories(ActiveSettings::GetCachePath("shaderCache/precompiled"), ec);
    m_archivePath = ActiveSettings::GetCachePath("shaderCache/precompiled/{:016x}_mtlarchive.bin", cacheTitleId);
    const fs::path keyPath = ActiveSettings::GetCachePath("shaderCache/precompiled/{:016x}_mtlarchive.key", cacheTitleId);

    // An archive created on another GPU, macOS build or by another archive version is useless, start over
    const std::string key = GetArchiveKey(device);
    if (ReadKeyFile(keyPath) != key)
    {
        if (fs::exists(m_archivePath, ec))
            cemuLog_log(LogType::Force, "Metal binary archive was created for another GPU or macOS version, rebuilding it");
        fs::remove(m_archivePath, ec);
        WriteKeyFile(keyPath, key);
    }

    const uintmax_t archiveSize = fs::file_size(m_archivePath, ec);
    if (!ec && archiveSize > METAL_BINARY_ARCHIVE_MAX_SIZE)
    {
        cemuLog_log(LogType::Force, "Metal binary archive exceeds {}MB, rebuilding it", METAL_BINARY_ARCHIVE_MAX_SIZE / 1024 / 1024);
        fs::remove(m_archivePath, ec);
    }

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    if (fs::exists(m_archivePath, ec))
    {
        // Two instances backed by the same file: lookups never touch the archive that is being modified
        m_loadedArchive = CreateArchive(device, &m_archivePath);
        if (m_loadedArchive)
            m_writeArchive = CreateArchive(device, &m_archivePath);
        if (!m_writeArchive)
        {
            cemuLog_log(LogType::Force, "Metal binary archive {} is invalid, rebuilding it", _pathToUtf8(m_archivePath));
            if (m_loadedArchive)
            {
                m_loadedArchive->release();
                m_loadedArchive = nullptr;
            }
            fs::remove(m_archivePath, ec);
        }
        else
        {
            cemuLog_log(LogType::Force, "Loaded Metal binary archive ({}KB)", archiveSize / 1024);
        }
    }
    if (!m_writeArchive)
        m_writeArchive = CreateArchive(device, nullptr);
    pool->release();

    if (!m_writeArchive)
    {
        cemuLog_log(LogType::Force, "Metal binary archive disabled");
        return;
    }

    m_hitCount = 0;
    m_missCount = 0;
    m_addedCount = 0;
    m_addErrorCount = 0;
    {
        std::unique_lock lock(m_mutex);
        m_isOpen = true;
        m_stopRequested = false;
    }
    m_workerThread = std::thread(&MetalBinaryArchive::WorkerThread, this);
}

void MetalBinaryArchive::Close()
{
    {
        std::unique_lock lock(m_mutex);
        if (!m_isOpen)
            return;
        m_isOpen = false;
        m_stopRequested = true;
    }
    m_condVar.notify_one();
    m_workerThread.join();

    cemuLog_log(LogType::Force, "Metal binary archive: {} hits, {} misses, {} pipelines added", m_hitCount.load(), m_missCount.load(), m_addedCount.load());

    if (m_loadedArchive)
    {
        m_loadedArchive->release();
        m_loadedArchive = nullptr;
    }
    m_writeArchive->release();
    m_writeArchive = nullptr;
}

bool MetalBinaryArchive::Attach(MTL::RenderPipelineDescriptor* desc)
{
    std::unique_lock lock(m_mutex);
    if (!m_isOpen || !m_loadedArchive)
        return false;

    const NS::Object* archives[] = {m_loadedArchive};
    NS_STACK_SCOPED NS::Array* archiveArray = NS::Array::alloc()->init(archives, 1);
    desc->setBinaryArchives(archiveArray);

    return true;
}

void MetalBinaryArchive::ReportLookup(bool hit)
{
    if (hit)
        m_hitCount++;
    else
        m_missCount++;
}

void MetalBinaryArchive::AddPipeline(MTL::RenderPipelineDescriptor* desc)
{
    {
        std::unique_lock lock(m_mutex);
        if (!m_isOpen)
            return;
        desc->retain();
        m_pendingDescriptors.push_back(desc);
    }
    m_condVar.notify_one();
}

void MetalBinaryArchive::WorkerThread()
{
    SetThreadName("mtlBinArchive");
    // Archive maintenance is never urgent and must not compete with emulation
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);

    auto lastSerializeTime = std::chrono::steady_clock::now();
    std::optional<std::chrono::steady_clock::time_point> flushDeadline;
    bool isDirty = false;

    std::unique_lock lock(m_mutex);
    while (true)
    {
        m_condVar.wait_for(lock, SERIALIZE_INTERVAL, [this]() { return !m_pendingDescriptors.empty() || m_stopRequested; });

        while (!m_pendingDescriptors.empty())
        {
            MTL::RenderPipelineDescriptor* desc = m_pendingDescriptors.front();
            m_pendingDescriptors.pop_front();
            if (m_stopRequested && !flushDeadline)
                flushDeadline = std::chrono::steady_clock::now() + CLOSE_FLUSH_TIMEOUT;
            const bool skip = flushDeadline && std::chrono::steady_clock::now() >= *flushDeadline;
            lock.unlock();

            if (!skip && AddToArchive(desc))
                isDirty = true;
            desc->release();

            lock.lock();
        }

        const bool stop = m_stopRequested;
        lock.unlock();
        if (isDirty && (stop || std::chrono::steady_clock::now() - lastSerializeTime >= SERIALIZE_INTERVAL))
        {
            Serialize();
            isDirty = false;
            lastSerializeTime = std::chrono::steady_clock::now();
        }
        lock.lock();

        if (stop)
            break;
    }
}

bool MetalBinaryArchive::AddToArchive(MTL::RenderPipelineDescriptor* desc)
{
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    NS::Error* error = nullptr;
    bool added = m_writeArchive->addRenderPipelineFunctions(desc, &error);
    if (added)
    {
        m_addedCount++;
    }
    else if (m_addErrorCount < 10)
    {
        m_addErrorCount++;
        cemuLog_log(LogType::Force, "Failed to add pipeline to Metal binary archive: {}", error ? error->localizedDescription()->utf8String() : "unknown error");
    }
    pool->release();

    return added;
}

void MetalBinaryArchive::Serialize()
{
    // Write to a temporary file first so that a crash never leaves a partially written archive behind
    fs::path tempPath = m_archivePath;
    tempPath += ".tmp";

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    NS_STACK_SCOPED NS::URL* url = NewFileURL(tempPath);
    NS::Error* error = nullptr;
    bool serialized = m_writeArchive->serializeToURL(url, &error);
    if (!serialized)
        cemuLog_log(LogType::Force, "Failed to serialize Metal binary archive: {}", error ? error->localizedDescription()->utf8String() : "unknown error");
    pool->release();

    std::error_code ec;
    if (serialized)
    {
        fs::rename(tempPath, m_archivePath, ec);
        if (ec)
            cemuLog_log(LogType::Force, "Failed to store Metal binary archive: {}", ec.message());
    }
    if (!serialized || ec)
        fs::remove(tempPath, ec);
}
