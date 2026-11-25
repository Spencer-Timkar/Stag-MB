#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

// The shared local activation is an offline cache, not DRM. No seller
// credentials or activation data belong in the plug-in's host state.
class LicenseManager : private juce::Thread
{
public:
    struct Reply { int status = 0; juce::var json; };
    using Transport = std::function<Reply(const juce::String&, const juce::StringPairArray&)>;

    static juce::File defaultFile();
    explicit LicenseManager(juce::File = defaultFile(), Transport = {});
    ~LicenseManager() override;

    bool isActive() const noexcept { return active.load(); }
    bool isBusy() const noexcept { return busy.load(); }
    juce::String message() const;
    bool activate(const juce::String& key);
    bool deactivate();
    void refreshFromDisk();

private:
    void run() override;
    bool begin(bool activation, juce::String key);
    void updateActivation(bool activation, const juce::String& key);
    void activateOnline(const juce::String& key, const juce::var& previous);
    void deactivateOnline(const juce::var& record);
    void setMessage(juce::String);
    juce::var read() const;
    bool save(const juce::var&) const;

    const juce::File file;
    const Transport transport;
    std::atomic<bool> active{false}, busy{false};
    mutable juce::CriticalSection mutex;
    juce::String status, pendingKey;
    bool pendingActivation = false;
    bool workerAvailable = false;
};
