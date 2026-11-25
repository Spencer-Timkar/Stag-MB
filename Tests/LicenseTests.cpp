#include "LicenseManager.h"

#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
const juce::String key = "test-license-key-00000001";
const juce::String otherKey = "test-license-key-00000002";
constexpr auto activationId = "test-activation";

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

void activate(LicenseManager& manager, const juce::String& licenseKey = key)
{
    check(manager.activate(" \t" + licenseKey + "\n"), "Activation request was not accepted");
    wait(manager);
}

void deactivate(LicenseManager& manager)
{
    check(manager.deactivate(), "Deactivation request was not accepted");
    wait(manager);
}

juce::String field(const juce::var& object, const char* name)
{
    const auto& value = object[name];
    return value.isString() ? value.toString() : juce::String{};
}

int propertyCount(const juce::var& object)
{
    const auto* dynamic = object.getDynamicObject();
    return dynamic != nullptr ? dynamic->getProperties().size() : -1;
}

bool isUuid(const juce::String& text)
{
    const juce::Uuid parsed(text);
    return text.length() == 36 && !parsed.isNull() && parsed.toDashedString() == text;
}

juce::var load(const juce::File& file)
{
    return juce::JSON::parse(file.loadFileAsString());
}

LicenseManager::Reply success(const juce::String& action)
{
    if (action == "deactivate")
        return {200, juce::JSON::parse(R"({"status":"deactivated"})")};
    auto response = juce::JSON::parse(R"({"status":"activated","activation_id":"test-activation","product":"motion-bands","activation_limit":3})");
    response.getDynamicObject()->setProperty("environment", STAG_LICENSE_ENVIRONMENT);
    return {200, response};
}

struct Call { juce::String action; juce::var body; };
struct Recorder
{
    std::vector<Call> calls;
    LicenseManager::Reply operator()(const juce::String& action, const juce::var& body)
    {
        // Copy the body so later mutation by the manager can't change what was sent.
        calls.push_back({action, juce::JSON::parse(juce::JSON::toString(body))});
        return success(action);
    }
};

void checkActivationBody(const Call& call, const juce::String& licenseKey)
{
    const auto& body = call.body;
    const auto installation = field(body, "installation_id");
    check(call.action == "activate" && propertyCount(body) == 6
              && field(body, "product") == "motion-bands"
              && field(body, "environment") == STAG_LICENSE_ENVIRONMENT
              && field(body, "key") == licenseKey
              && isUuid(installation) && isUuid(field(body, "attempt_id"))
              && field(body, "label").startsWith("Motion Bands ")
              && field(body, "label").endsWith(" " + installation.substring(0, 8))
              && !field(body, "label").contains(licenseKey),
          "Activation sent unexpected product, environment, key or device data");
}

void checkDeactivationBody(const Call& call, const juce::String& installation)
{
    const auto& body = call.body;
    check(call.action == "deactivate" && propertyCount(body) == 5
              && field(body, "product") == "motion-bands"
              && field(body, "environment") == STAG_LICENSE_ENVIRONMENT
              && field(body, "key") == key && field(body, "activation_id") == activationId
              && field(body, "installation_id") == installation,
          "Deactivation sent unexpected key, activation or installation");
}

void checkActiveRecord(const juce::File& file, const juce::String& installation)
{
    const auto record = load(file);
    check(record["schema"].isInt() && static_cast<int>(record["schema"]) == 2
              && field(record, "product") == "motion-bands"
              && field(record, "environment") == STAG_LICENSE_ENVIRONMENT
              && field(record, "installation_id") == installation
              && record["active"].isBool() && static_cast<bool>(record["active"])
              && field(record, "key") == key && field(record, "activation_id") == activationId
              && record.hasProperty("pending_attempt_id") && record["pending_attempt_id"].isVoid(),
          "Activated record doesn't match schema 2");
}

void checkClearedRecord(const juce::File& file, const char* label)
{
    const auto record = load(file);
    check(record["active"].isBool() && !static_cast<bool>(record["active"])
              && record["key"].isVoid() && record["activation_id"].isVoid() && record["pending_attempt_id"].isVoid()
              && isUuid(field(record, "installation_id")),
          label);
}

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
    juce::WaitableEvent activationStarted, allowActivation;
    bool activationResumed = false;
    LicenseManager owner(file, [&](const auto& action, const auto& body) {
        if (action == "activate")
        {
            activationStarted.signal();
            activationResumed = allowActivation.wait(12000);
        }
        return onlineRequests(action, body);
    });
    LicenseManager otherFormat(file, std::ref(concurrentRequests));
    check(!owner.isActive(), "New installation was active");
    check(!owner.activate("short"), "Short key accepted");
    check(owner.activate(key), "Couldn't start the owner activation");
    {
        struct ResumeOnExit { juce::WaitableEvent& event; ~ResumeOnExit() { event.signal(); } } resume{allowActivation};
        check(activationStarted.wait(5000), "Activation didn't start");
        const auto pending = load(file);
        check(isUuid(field(pending, "pending_attempt_id")) && isUuid(field(pending, "installation_id"))
                  && !static_cast<bool>(pending["active"]),
              "Installation and attempt ids weren't persisted before the request");
        check(!owner.deactivate(), "Manager accepted a second in-flight job");
        activate(otherFormat);
        check(!otherFormat.isActive() && concurrentRequests.calls.empty(), "Another format used a second slot");
        useSeparateProcess("--reject-concurrent", file);
    }
    wait(owner);
    check(activationResumed && owner.isActive(), "Activation failed");
    check(onlineRequests.calls.size() == 1, "Activation must be a single request");
    checkActivationBody(onlineRequests.calls[0], key);
    const auto installation = field(onlineRequests.calls[0].body, "installation_id");
    checkActiveRecord(file, installation);

    // Other formats in the same host share the activation without a request.
    activate(otherFormat);
    check(otherFormat.isActive() && concurrentRequests.calls.empty(), "Existing activation used another slot");

    Recorder offlineRequests;
    LicenseManager offline(file, std::ref(offlineRequests));
    check(offline.isActive() && offlineRequests.calls.empty(), "Offline reopen did not trust the record");
    useSeparateProcess("--release", file);
    waitFor([&] { return !offline.isActive(); }, "Another host's release did not refresh automatically");
    check(offlineRequests.calls.empty(), "Automatic refresh went online");
    checkClearedRecord(file, "Released record kept credentials");
    check(field(load(file), "installation_id") == installation, "Release changed the installation id");
}

void deniedActivation(const juce::File& folder)
{
    for (const auto denied : {400, 403, 404, 409, 410})
    {
        Recorder requests;
        const auto file = folder.getChildFile("denied-" + juce::String(denied) + ".json");
        LicenseManager manager(file, [&](const auto& action, const auto& body) {
            auto reply = requests(action, body);
            if (requests.calls.size() == 1)
                reply = {denied, juce::var{}};
            return reply;
        });
        activate(manager);
        check(!manager.isActive() && requests.calls.size() == 1, "Denied activation left this installation active");
        checkActivationBody(requests.calls[0], key);
        checkClearedRecord(file, "Denied activation kept the key or the settled attempt");
        check(!manager.message().contains(key), "Denial status leaked the key");
        manager.refreshFromDisk();
        check(!manager.isActive(), "Denied activation was persisted as active");

        activate(manager);
        check(manager.isActive() && requests.calls.size() == 2, "Activation after a denial failed");
        const auto& first = requests.calls[0].body;
        const auto& second = requests.calls[1].body;
        check(field(second, "attempt_id") != field(first, "attempt_id")
                  && field(second, "installation_id") == field(first, "installation_id"),
              "A settled attempt id was reused or the installation id changed");
    }
}

void pendingRetry(const juce::File& folder)
{
    for (int scenario = 0; scenario < 10; ++scenario)
    {
        const auto file = folder.getChildFile("pending-" + juce::String(scenario) + ".json");
        juce::String firstAttempt, firstInstallation;
        {
            Recorder requests;
            LicenseManager manager(file, [&](const auto& action, const auto& body) {
                auto reply = requests(action, body);
                auto* response = reply.json.getDynamicObject();
                switch (scenario)
                {
                    case 0: reply = {202, juce::JSON::parse(R"({"status":"pending"})")}; break;
                    case 1: reply = {}; break;
                    case 2: reply = {503, juce::var{}}; break;
                    case 3: reply = {429, juce::var{}}; break;
                    case 4: response->setProperty("product", "limiter"); break;
                    case 5: response->setProperty("environment", "wrong"); break;
                    case 6: response->setProperty("activation_limit", 4); break;
                    case 7: response->removeProperty("activation_id"); break;
                    case 8: response->setProperty("status", "pending"); break;
                    case 9: reply.status = 302; break;
                }
                return reply;
            });
            activate(manager);
            check(!manager.isActive() && requests.calls.size() == 1, "Unsettled activation became active");
            checkActivationBody(requests.calls[0], key);
            firstAttempt = field(requests.calls[0].body, "attempt_id");
            firstInstallation = field(requests.calls[0].body, "installation_id");
            const auto pending = load(file);
            check(!static_cast<bool>(pending["active"]) && field(pending, "pending_attempt_id") == firstAttempt
                      && field(pending, "installation_id") == firstInstallation && field(pending, "key") == key,
                  "Unsettled attempt wasn't kept for a retry");
            check(!manager.message().contains(key), "Pending status leaked the key");
        }

        // Retry after reopening the host: same attempt and installation, no extra slot.
        Recorder retry;
        LicenseManager reopened(file, std::ref(retry));
        check(!reopened.isActive() && retry.calls.empty(), "A pending attempt was trusted or went online on reopen");
        if (scenario == 0)
        {
            // A different key settles separately under a fresh attempt id.
            Recorder other;
            LicenseManager otherKeyManager(file.getSiblingFile("pending-other-key.json"), std::ref(other));
            check(file.copyFileTo(file.getSiblingFile("pending-other-key.json")), "Couldn't copy the pending record");
            otherKeyManager.refreshFromDisk();
            activate(otherKeyManager, otherKey);
            check(other.calls.size() == 1 && field(other.calls[0].body, "attempt_id") != firstAttempt
                      && field(other.calls[0].body, "installation_id") == firstInstallation,
                  "Another key reused a pending attempt id");
        }
        activate(reopened);
        check(reopened.isActive() && retry.calls.size() == 1, "Retry didn't activate");
        checkActivationBody(retry.calls[0], key);
        check(field(retry.calls[0].body, "attempt_id") == firstAttempt
                  && field(retry.calls[0].body, "installation_id") == firstInstallation,
              "Retry didn't reuse the pending attempt and installation ids");
        checkActiveRecord(file, firstInstallation);
    }
}

void rejectForeignRecords(const juce::File& folder)
{
    Recorder requests;
    const auto file = folder.getChildFile("foreign.json");
    LicenseManager manager(file, std::ref(requests));
    activate(manager);
    check(manager.isActive(), "Could not prepare a record for validation");
    const auto record = file.loadFileAsString();

    struct Mutation { const char* property; juce::var value; bool remove; };
    const std::vector<Mutation> mutations{
        {"product", "limiter", false},
        {"environment", juce::String(STAG_LICENSE_ENVIRONMENT) == "sandbox" ? "production" : "sandbox", false},
        {"schema", 1, false},
        {"schema", 3, false},
        {"schema", "2", false},
        {"active", "true", false},
        {"installation_id", "not-a-uuid", false},
        {"key", "short", false},
        {"activation_id", {}, true},
        {"key", {}, true},
        {"installation_id", {}, true},
    };
    for (const auto& mutation : mutations)
    {
        auto foreign = juce::JSON::parse(record);
        if (mutation.remove)
            foreign.getDynamicObject()->removeProperty(mutation.property);
        else
            foreign.getDynamicObject()->setProperty(mutation.property, mutation.value);
        check(file.replaceWithText(juce::JSON::toString(foreign)), "Couldn't write a foreign record");
        manager.refreshFromDisk();
        check(!manager.isActive(), "A record with invalid identity was trusted");
    }
    check(file.replaceWithText("malformed"), "Couldn't write malformed record");
    manager.refreshFromDisk();
    check(!manager.isActive(), "Malformed record accepted");
    auto oversized = juce::String::repeatedString(" ", 16 * 1024) + record;
    check(file.replaceWithText(oversized), "Couldn't write oversized record");
    manager.refreshFromDisk();
    check(!manager.isActive() && requests.calls.size() == 1, "Oversized record accepted or refresh went online");

    // A pending record for another product contributes neither its installation nor its attempt id.
    auto foreign = juce::JSON::parse(record);
    auto* object = foreign.getDynamicObject();
    object->setProperty("product", "limiter");
    object->setProperty("active", false);
    object->setProperty("pending_attempt_id", juce::Uuid().toDashedString());
    check(file.replaceWithText(juce::JSON::toString(foreign)), "Couldn't write a foreign pending record");
    activate(manager);
    check(manager.isActive() && requests.calls.size() == 2, "Activation over a foreign record failed");
    const auto& body = requests.calls[1].body;
    check(field(body, "attempt_id") != field(foreign, "pending_attempt_id")
              && field(body, "installation_id") != field(foreign, "installation_id"),
          "A foreign record's identifiers were reused");
}

bool obstructReplacement(const juce::File& file, const juce::File& staged)
{
    return file.moveFileTo(staged) && file.createDirectory().wasOk();
}

void restoreAccess(const juce::File& file, const juce::File& staged)
{
    check(file.deleteFile() && staged.moveFileTo(file), "Couldn't restore record access");
}

void activationSaveFailure(const juce::File& folder)
{
    Recorder requests;
    const auto file = folder.getChildFile("save-failure.json");
    const auto staged = file.getSiblingFile(file.getFileName() + ".staged");
    bool obstructed = false;
    LicenseManager manager(file, [&](const auto& action, const auto& body) {
        if (requests.calls.empty())
            obstructed = obstructReplacement(file, staged);
        return requests(action, body);
    });
    activate(manager);
    check(obstructed, "Couldn't simulate post-activation write failure");
    check(!manager.isActive() && requests.calls.size() == 1, "Unsaved activation reported active");
    check(manager.message().contains("another computer slot") && !manager.message().contains(key),
          "Save failure status incorrect or leaked the key");
    restoreAccess(file, staged);
    manager.refreshFromDisk();
    check(!manager.isActive(), "Pending record was trusted");

    activate(manager);
    check(manager.isActive() && requests.calls.size() == 2
              && field(requests.calls[1].body, "attempt_id") == field(requests.calls[0].body, "attempt_id"),
          "Retry after a save failure didn't reuse the attempt id");
    checkActiveRecord(file, field(requests.calls[0].body, "installation_id"));
}

void deactivationFailures(const juce::File& folder)
{
    for (int scenario = 0; scenario < 6; ++scenario)
    {
        Recorder requests;
        const auto file = folder.getChildFile("release-" + juce::String(scenario) + ".json");
        const auto staged = file.getSiblingFile(file.getFileName() + ".staged");
        bool obstructed = false;
        LicenseManager manager(file, [&](const auto& action, const auto& body) {
            auto reply = requests(action, body);
            if (action == "deactivate")
            {
                if (scenario >= 4)
                    obstructed = obstructReplacement(file, staged);
                switch (scenario)
                {
                    case 0: case 4: return LicenseManager::Reply{};
                    case 1: return LicenseManager::Reply{200, juce::JSON::parse(R"({"status":"activated"})")};
                    case 2: return LicenseManager::Reply{503, juce::var{}};
                    case 3: return LicenseManager::Reply{404, juce::var{}};
                    default: break;
                }
            }
            return reply;
        });
        activate(manager);
        check(manager.isActive(), "Couldn't prepare local activation for release");
        const auto installation = field(requests.calls[0].body, "installation_id");
        deactivate(manager);
        check(requests.calls.size() == 2, "Release didn't send exactly one request");
        checkDeactivationBody(requests.calls[1], installation);
        if (scenario >= 4)
        {
            check(obstructed, "Couldn't simulate write failure during release");
            restoreAccess(file, staged);
        }
        const auto restored = scenario < 4;
        check(manager.isActive() == restored, "Incorrect in-memory activation after release attempt");
        manager.refreshFromDisk();
        check(manager.isActive() == restored, "Incorrect persisted activation after release attempt");
        if (restored)
            checkActiveRecord(file, installation);
        else
            checkClearedRecord(file, "Released record kept credentials");
        check(manager.message().contains(restored ? "unchanged" : scenario == 4 ? "contact support" : "Installation released")
                  && !manager.message().contains(key),
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
        check(defaultFile.getFileName() == "activation-v2-" STAG_LICENSE_ENVIRONMENT ".json"
                  && defaultFile.getParentDirectory().getFileName() == "Motion Bands"
                  && defaultFile.getParentDirectory().getParentDirectory().getFileName() == "Stag Audio",
              "Default record isn't scoped to Motion Bands and its environment");
        concurrentAndOffline(folder.getChildFile("shared.json"));
        deniedActivation(folder);
        pendingRetry(folder);
        rejectForeignRecords(folder);
        activationSaveFailure(folder);
        deactivationFailures(folder);
        std::cout << "PASS: Motion Bands activation, pending retry, offline record, format and process locking, "
                     "record isolation and deactivation\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
