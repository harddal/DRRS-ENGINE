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
    }

    void flush_() override {}

private:
    std::vector<ImGuiLogEntry> m_entries;
};
