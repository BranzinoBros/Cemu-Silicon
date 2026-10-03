#pragma once

#include <Metal/Metal.hpp>

#include <condition_variable>
#include <deque>

// Per-title MTLBinaryArchive with the GPU binaries of render pipelines created in previous sessions
// Pipelines found in the archive are loaded instead of being compiled for the GPU again
// The archive is only valid for the GPU and macOS build it was created on, a mismatch, a corrupt file or any Metal error discards it
class MetalBinaryArchive
{
public:
    MetalBinaryArchive() = default;
    ~MetalBinaryArchive();

    void Open(MTL::Device* device, uint64 cacheTitleId);
    void Close(); // flushes queued pipelines and serializes the archive, called on title exit

    // Lets the descriptor look up its pipeline in the archive. Returns false if there is nothing to look up
    bool Attach(MTL::RenderPipelineDescriptor* desc);
    void ReportLookup(bool hit);

    // Queues the descriptor of a successfully created pipeline for insertion into the archive
    void AddPipeline(MTL::RenderPipelineDescriptor* desc);

    // Debug
    uint32 GetHitCount() const { return m_hitCount; }
    uint32 GetMissCount() const { return m_missCount; }
    uint32 GetAddedCount() const { return m_addedCount; }

private:
    // archive used for lookups, loaded from disk and never modified
    MTL::BinaryArchive* m_loadedArchive = nullptr;
    // archive that new pipelines are added to and which gets serialized, only accessed by the worker thread
    MTL::BinaryArchive* m_writeArchive = nullptr;

    fs::path m_archivePath;
    bool m_isOpen = false;
    bool m_stopRequested = false;
    std::deque<MTL::RenderPipelineDescriptor*> m_pendingDescriptors;
    std::mutex m_mutex;
    std::condition_variable m_condVar;
    std::thread m_workerThread;

    std::atomic_uint32_t m_hitCount{0};
    std::atomic_uint32_t m_missCount{0};
    std::atomic_uint32_t m_addedCount{0};
    uint32 m_addErrorCount = 0; // only accessed by the worker thread

    void WorkerThread();
    bool AddToArchive(MTL::RenderPipelineDescriptor* desc);
    void Serialize();
};
