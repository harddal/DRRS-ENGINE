#pragma once

#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/log_msg.h>
#include <mutex>
#include <string>
#include <vector>

struct ImGuiLogEntry
{
    spdlog::level::level_enum level;
    std::string               text;
};

class ImGuiLogSink final : public spdlog::sinks::base_sink<std::mutex>
{
public:
    static constexpr size_t k_maxEntries = 512;

    static std::shared_ptr<ImGuiLogSink> instance()
    {
        static auto s = std::make_shared<ImGuiLogSink>();
        return s;
    }

    // Safe to call from the render thread — snapshots under lock.
    std::vector<ImGuiLogEntry> snapshot()
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return m_entries;
    }

    // Incremental drain for consumers that mirror the log (e.g. GameConsole):
    // appends entries with sequence >= 'seq' to out, returns the next sequence
    // to pass. Handles the ring evicting old entries (seq gaps are skipped).
    uint64_t entriesSince(uint64_t seq, std::vector<ImGuiLogEntry>& out)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        uint64_t first = m_totalPushed - m_entries.size(); // sequence of m_entries[0]
        if (seq < first)
            seq = first;
        for (size_t i = static_cast<size_t>(seq - first); i < m_entries.size(); ++i)
            out.push_back(m_entries[i]);
        return m_totalPushed;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lk(mutex_);
        m_entries.clear();
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t buf;
        formatter_->format(msg, buf);
        if (m_entries.size() >= k_maxEntries)
            m_entries.erase(m_entries.begin());
        m_entries.push_back({ msg.level, std::string(buf.begin(), buf.end()) });
        m_totalPushed++;
    }

    void flush_() override {}

private:
    std::vector<ImGuiLogEntry> m_entries;
    uint64_t m_totalPushed = 0; // monotonic — drives entriesSince()
};
