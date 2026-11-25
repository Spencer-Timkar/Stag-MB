#include "LicenseManager.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
const juce::String key = "test-license-key-00000001";
constexpr auto instanceId = "test-instance";

void check(bool ok, const char* label)
{
    if (!ok)
        throw std::runtime_error(label);
}

template <typename Condition>
void waitFor(Condition condition, const char* label)
{
    for (int i = 0; i < 1000 && !condition(); ++i)
        juce::Thread::sleep(5);
    check(condition(), label);
}

void wait(LicenseManager& manager)
{
    waitFor([&] { return !manager.isBusy(); }, "Activation worker timed out");
}

void activate(LicenseManager& manager)
{
    check(manager.activate(" \t" + key + "\n"), "Activation request was not accepted");
    wait(manager);
}

void deactivate(LicenseManager& manager)
{
    check(manager.deactivate(), "Deactivation request was not accepted");
    wait(manager);
}

LicenseManager::Reply success(const juce::String& action)
{
    auto response = juce::JSON::parse(R"({"meta":{},"license_key":{"activation_limit":3,"expires_at":null},"instance":{"id":"test-instance"}})");
    auto* meta = response["meta"].getDynamicObject();
    meta->setProperty("store_id", juce::String(STAG_LICENSE_STORE_ID).getLargeIntValue());
    meta->setProperty("product_id", juce::String(STAG_LICENSE_PRODUCT_ID).getLargeIntValue());
    meta->setProperty("variant_id", juce::String(STAG_LICENSE_VARIANT_ID).getLargeIntValue());
    response.getDynamicObject()->setProperty(action == "validate" ? "valid" : action == "activate" ? "activated" : "deactivated", true);
    return {200, response};
}

struct Call { juce::String action; juce::StringPairArray fields; };
struct Recorder
{
    std::vector<Call> calls;
    LicenseManager::Reply operator()(const juce::String& action, const juce::StringPairArray& fields)
    {
        calls.push_back({action, fields});
        return success(action);
    }
};

void useSeparateProcess(const char* mode, const juce::File& file)
{
    juce::ChildProcess child;
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();
    const juce::StringArray args{executable, mode, file.getParentDirectory().getFileName(), file.getFileName()};
    check(child.start(args, 0), "Couldn't start the second host process");
    if (!child.waitForProcessToFinish(7000))
    {
        child.kill();
        check(false, "Second host process timed out");
    }
    check(child.getExitCode() == 0, "Second host process failed its activation assertion");
}

void concurrentAndOffline(const juce::File& file)
{
    Recorder onlineRequests, concurrentRequests;
    juce::WaitableEvent validationStarted, allowValidation;
    bool validationResumed = false;
    LicenseManager owner(file, [&](const auto& action, const auto& fields) {
        if (action == "validate")
        {
            validationStarted.signal();
            validationResumed = allowValidation.wait(12000);
        }
        return onlineRequests(action, fields);
    });
    LicenseManager otherFormat(file, std::ref(concurrentRequests));
    check(!owner.isActive(), "New installation was active");
    check(!owner.activate("short"), "Short key accepted");
    check(owner.activate(key), "Couldn't start the owner activation");
    {
        struct ResumeOnExit { juce::WaitableEvent& event; ~ResumeOnExit() { event.signal(); } } resume{allowValidation};
        check(validationStarted.wait(5000), "Validation didn't start");
        check(!owner.deactivate(), "Manager accepted a second in-flight job");
        activate(otherFormat);
        check(!otherFormat.isActive() && concurrentRequests.calls.empty(), "Another format used a second slot");
        useSeparateProcess("--reject-concurrent", file);
    }
    wait(owner);
    check(validationResumed && owner.isActive(), "Activation failed");
    check(onlineRequests.calls.size() == 2 && onlineRequests.calls[0].action == "validate"
          && onlineRequests.calls[1].action == "activate", "Activation must validate before using a slot");
    check(onlineRequests.calls[0].fields.size() == 1 && onlineRequests.calls[0].fields["license_key"] == key,
          "Validation sent unexpected fields");
    const auto& allocation = onlineRequests.calls[1].fields;
    check(allocation.size() == 2 && allocation["license_key"] == key && allocation["instance_name"].startsWith("Motion Bands "),
          "Activation sent unexpected key or device data");
    activate(otherFormat);
    check(otherFormat.isActive() && concurrentRequests.calls.empty(), "Existing activation used another slot");

    Recorder offlineRequests;
    LicenseManager offline(file, std::ref(offlineRequests));
    check(offline.isActive() && offlineRequests.calls.empty(), "Offline reopen did not trust the cache");
    useSeparateProcess("--release", file);
    waitFor([&] { return !offline.isActive(); }, "Another host's release did not refresh automatically");
    check(offlineRequests.calls.empty(), "Automatic refresh went online");
    const auto released = juce::JSON::parse(file.loadFileAsString());
    check(!released.hasProperty("key") && !released.hasProperty("instance_id"), "Released cache kept credentials");
}

void rejectValidation(const juce::File& folder)
{
    for (int scenario = 0; scenario < 9; ++scenario)
    {
        Recorder requests;
        const auto file = folder.getChildFile("invalid-" + juce::String(scenario) + ".json");
        LicenseManager manager(file, [&](const auto& action, const auto& fields) {
            auto reply = requests(action, fields);
            auto* meta = reply.json["meta"].getDynamicObject();
            auto* license = reply.json["license_key"].getDynamicObject();
            switch (scenario)
            {
                case 0: meta->setProperty("store_id", "wrong"); break;
                case 1: meta->setProperty("product_id", "limiter"); break;
                case 2: meta->setProperty("variant_id", "wrong"); break;
                case 3: license->setProperty("activation_limit", 4); break;
                case 4: license->setProperty("expires_at", "2030-01-01"); break;
                case 5: license->removeProperty("expires_at"); break;
                case 6: reply.json.getDynamicObject()->setProperty("valid", "true"); break;
                case 7: reply.status = 302; break;
                case 8: reply = {}; break;
            }
            return reply;
        });
        activate(manager);
        check(!manager.isActive() && !file.existsAsFile() && requests.calls.size() == 1
              && requests.calls.front().action == "validate", "Invalid validation response consumed a slot");
    }
}

void rejectForeignCaches(const juce::File& folder)
{
    Recorder requests;
    const auto file = folder.getChildFile("foreign.json");
    LicenseManager manager(file, std::ref(requests));
    activate(manager);
    check(manager.isActive(), "Could not prepare a cache for record validation");
    const auto record = file.loadFileAsString();
    for (const auto* property : {"environment", "product", "store_id", "product_id", "variant_id", "schema", "instance_id"})
    {
        auto foreign = juce::JSON::parse(record);
        const juce::String name(property);
        if (name == "instance_id")
            foreign.getDynamicObject()->removeProperty(property);
        else
            foreign.getDynamicObject()->setProperty(property, name == "product" ? "limiter" : "wrong");
        check(file.replaceWithText(juce::JSON::toString(foreign)), "Couldn't write a foreign record");
        manager.refreshFromDisk();
        check(!manager.isActive(), "A cache with invalid identity was trusted");
    }
    check(file.replaceWithText("malformed"), "Couldn't write malformed record");
    manager.refreshFromDisk();
    check(!manager.isActive(), "Malformed cache accepted");
    auto oversized = juce::String::repeatedString(" ", 16 * 1024) + record;
    check(file.replaceWithText(oversized), "Couldn't write oversized record");
    manager.refreshFromDisk();
    check(!manager.isActive() && requests.calls.size() == 2, "Oversized cache accepted or cache refresh went online");
}

bool obstructReplacement(const juce::File& file, const juce::File& staged)
{
    return file.moveFileTo(staged) && file.createDirectory().wasOk();
}

void restoreAccess(const juce::File& file, const juce::File& staged)
{
    check(file.deleteFile() && staged.moveFileTo(file), "Couldn't restore cache access");
}

void activationRollback(const juce::File& folder)
{
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        Recorder requests;
        const auto file = folder.getChildFile("rollback-" + juce::String(scenario) + ".json");
        const auto staged = file.getSiblingFile(file.getFileName() + ".staged");
        bool obstructed = false;
        LicenseManager manager(file, [&](const auto& action, const auto& fields) {
            auto reply = requests(action, fields);
            if (action == "activate")
            {
                if (scenario == 0)
                    reply.json["meta"].getDynamicObject()->setProperty("variant_id", "wrong");
                else
                    obstructed = obstructReplacement(file, staged);
            }
            if (action == "deactivate" && scenario == 2)
                return LicenseManager::Reply{};
            return reply;
        });
        activate(manager);
        check(!manager.isActive() && requests.calls.size() == 3 && requests.calls.back().action == "deactivate"
              && requests.calls.back().fields["instance_id"] == instanceId && requests.calls.back().fields["license_key"] == key,
              "An unusable activation didn't attempt to release the allocated instance");
        check(manager.message().contains(scenario == 2 ? "Contact support" : "slot was released")
              && !manager.message().contains(key), "Rollback status incorrect or leaked the key");
        if (scenario != 0)
        {
            check(obstructed, "Couldn't simulate post-activation write failure");
            restoreAccess(file, staged);
        }
        manager.refreshFromDisk();
        check(!manager.isActive() && !juce::JSON::parse(file.loadFileAsString()).hasProperty("key"),
              "Rolled back activation was cached");
    }
}

void deactivationFailures(const juce::File& folder)
{
    for (int scenario = 0; scenario < 4; ++scenario)
    {
        Recorder requests;
        const auto file = folder.getChildFile("release-" + juce::String(scenario) + ".json");
        const auto staged = file.getSiblingFile(file.getFileName() + ".staged");
        bool obstructed = false;
        LicenseManager manager(file, [&](const auto& action, const auto& fields) {
            auto reply = requests(action, fields);
            if (action == "deactivate")
            {
                if (scenario >= 2)
                    obstructed = obstructReplacement(file, staged);
                if (scenario == 1)
                    reply.json["meta"].getDynamicObject()->setProperty("product_id", "wrong");
                if (scenario == 0 || scenario == 2)
                    return LicenseManager::Reply{};
            }
            return reply;
        });
        activate(manager);
        check(manager.isActive(), "Couldn't prepare local activation for release");
        deactivate(manager);
        check(requests.calls.size() == 3 && requests.calls.back().fields["instance_id"] == instanceId,
              "Wrong instance sent for release");
        if (scenario >= 2)
        {
            check(obstructed, "Couldn't simulate write failure during release");
            restoreAccess(file, staged);
        }
        const auto restored = scenario < 2;
        check(manager.isActive() == restored, "Incorrect in-memory activation after release attempt");
        manager.refreshFromDisk();
        check(manager.isActive() == restored, "Incorrect persisted activation after release attempt");
        check(juce::JSON::parse(file.loadFileAsString()).hasProperty("key") == restored, "Wrong cache credential state after release");
        check(manager.message().contains(restored ? "unchanged" : scenario == 2 ? "contact support" : "Installation released"),
              "Wrong release outcome status");
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc == 4)
        {
            Recorder requests;
            const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getChildFile(argv[2]).getChildFile(argv[3]);
            LicenseManager manager(file, std::ref(requests));
            const auto mode = juce::String::fromUTF8(argv[1]);
            if (mode == "--release")
            {
                check(manager.isActive() && requests.calls.empty(), "Second host did not activate offline");
                deactivate(manager);
                check(!manager.isActive() && requests.calls.size() == 1 && requests.calls.front().action == "deactivate",
                      "Second host did not release the shared activation");
            }
            else
            {
                check(mode == "--reject-concurrent", "Unexpected child process mode");
                activate(manager);
                check(!manager.isActive() && requests.calls.empty(), "Second host consumed a slot while the lock was held");
            }
            return 0;
        }

        const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("stag-motion-bands-license-test-" + juce::Uuid().toString());
        check(folder.createDirectory().wasOk(), "Couldn't create temporary license test directory");
        struct Cleanup { juce::File folder; ~Cleanup() { folder.deleteRecursively(); } } cleanup{folder};

        const auto defaultFile = LicenseManager::defaultFile();
        check(defaultFile.getFileName() == "activation-" STAG_LICENSE_ENVIRONMENT ".json"
              && defaultFile.getParentDirectory().getFileName() == "Motion Bands", "Default cache isn't scoped to Motion Bands and its environment");
        concurrentAndOffline(folder.getChildFile("shared.json"));
        rejectValidation(folder);
        rejectForeignCaches(folder);
        activationRollback(folder);
        deactivationFailures(folder);
        std::cout << "PASS: Motion Bands validation, offline cache, format and process locking, activation rollback and deactivation\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
