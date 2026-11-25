#include "LicenseManager.h"

#include <cstring>
#include <string_view>
#include <utility>

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <cstdio>
 #include <fcntl.h>
 #include <sys/file.h>
 #include <unistd.h>
#endif

namespace {
constexpr std::string_view environment = STAG_LICENSE_ENVIRONMENT;
constexpr std::string_view activationUrl = STAG_ACTIVATION_URL;
constexpr auto productSlug = "motion-bands";
constexpr int recordSchema = 2;
constexpr int activationLimit = 3;
constexpr int recordLimit = 16 * 1024;
constexpr int responseLimit = 32 * 1024;

#if JUCE_MAC
constexpr auto platformName = "macOS";
#elif JUCE_WINDOWS
constexpr auto platformName = "Windows";
#else
constexpr auto platformName = "Linux";
#endif

static_assert(environment == "sandbox" || environment == "production");
static_assert(activationUrl.starts_with("https://") && activationUrl.size() > 8 && !activationUrl.ends_with('/'));

// A file lock coordinates separately loaded plug-in binaries in the same host
// as well as other host processes. It is scoped to this user's activation file.
class ActivationLock
{
public:
    explicit ActivationLock(const juce::File& path)
    {
#if JUCE_WINDOWS
        handle = CreateFileW(path.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        OVERLAPPED offset{};
        held = handle != INVALID_HANDLE_VALUE
            && LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &offset);
#else
        handle = ::open(path.getFullPathName().toRawUTF8(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        held = handle >= 0 && ::flock(handle, LOCK_EX | LOCK_NB) == 0;
#endif
    }

    ~ActivationLock()
    {
#if JUCE_WINDOWS
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
#else
        if (handle >= 0)
            ::close(handle);
#endif
    }

    explicit operator bool() const noexcept { return held; }
    ActivationLock(const ActivationLock&) = delete;
    ActivationLock& operator=(const ActivationLock&) = delete;

private:
#if JUCE_WINDOWS
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
    bool held = false;
};

juce::var readJson(juce::InputStream& stream, int limit)
{
    if (stream.getTotalLength() > limit)
        return {};

    juce::MemoryBlock bytes;
    stream.readIntoMemoryBlock(bytes, limit + 1);
    if (bytes.isEmpty() || bytes.getSize() > static_cast<size_t>(limit))
        return {};
    const auto* data = static_cast<const char*>(bytes.getData());
    const auto length = static_cast<int>(bytes.getSize());
    if (std::memchr(data, 0, bytes.getSize()) != nullptr || !juce::CharPointer_UTF8::isValidString(data, length))
        return {};
    return juce::JSON::parse(juce::String::fromUTF8(data, length));
}

bool isTrue(const juce::var& value)
{
    return value.isBool() && static_cast<bool>(value);
}

juce::String stringField(const juce::var& object, const char* name)
{
    const auto& value = object[name];
    return value.isString() ? value.toString() : juce::String{};
}

bool isUuid(const juce::String& text)
{
    if (text.length() != 36)
        return false;
    const juce::Uuid parsed(text);
    return !parsed.isNull() && parsed.toDashedString() == text;
}

bool validKeyLength(const juce::String& key)
{
    const auto length = key.length();
    return length >= 16 && length <= 128;
}

bool validActivationId(const juce::String& id)
{
    return id.isNotEmpty() && id.length() <= 128;
}

bool recordMatchesBuild(const juce::var& record)
{
    return record["schema"].isInt() && static_cast<int>(record["schema"]) == recordSchema
        && stringField(record, "product") == productSlug
        && stringField(record, "environment") == STAG_LICENSE_ENVIRONMENT;
}

bool isActiveRecord(const juce::var& record)
{
    return recordMatchesBuild(record) && isTrue(record["active"])
        && validKeyLength(stringField(record, "key"))
        && validActivationId(stringField(record, "activation_id"))
        && isUuid(stringField(record, "installation_id"));
}

// An inactive schema-2 record. Key, activation and pending attempt start empty.
juce::var inactiveRecord(const juce::String& installation)
{
    auto* object = new juce::DynamicObject();
    juce::var record(object);
    object->setProperty("schema", recordSchema);
    object->setProperty("product", productSlug);
    object->setProperty("environment", STAG_LICENSE_ENVIRONMENT);
    object->setProperty("installation_id", installation);
    object->setProperty("active", false);
    object->setProperty("key", juce::var());
    object->setProperty("activation_id", juce::var());
    object->setProperty("pending_attempt_id", juce::var());
    return record;
}

bool isActivation(const juce::var& response)
{
    const auto& limit = response["activation_limit"];
    return stringField(response, "status") == "activated"
        && validActivationId(stringField(response, "activation_id"))
        && stringField(response, "product") == productSlug
        && stringField(response, "environment") == STAG_LICENSE_ENVIRONMENT
        && limit.isInt() && static_cast<int>(limit) == activationLimit;
}

// Answers that settle an attempt; anything else may have allocated a slot, so
// the attempt id is kept for the next try.
bool isDefinitiveDenial(int status)
{
    return status == 400 || status == 403 || status == 404 || status == 409 || status == 410;
}

juce::String activationOutcome(int status)
{
    switch (status)
    {
        case 202: return "Activation is still being confirmed. Try again in a moment; it won't use another computer slot.";
        case 400: return "Activation request was rejected. Update Stag Motion Bands and try again.";
        case 403: return "This key doesn't match this build. Download Stag Motion Bands from My tools.";
        case 404: return "This key can't activate Stag Motion Bands. Copy the Motion Bands key from My tools.";
        case 409: return "This key is already active on three computers. Deactivate one of them, then try again.";
        case 410: return "This license has been revoked. Contact Stag Audio support.";
        case 429: return "Too many activation attempts. Wait a minute and try again.";
        default:  return "Couldn't reach activation. Check your connection and try again.";
    }
}

juce::var requestBody(const juce::String& key, const juce::String& installation)
{
    auto* object = new juce::DynamicObject();
    juce::var body(object);
    object->setProperty("product", productSlug);
    object->setProperty("environment", STAG_LICENSE_ENVIRONMENT);
    object->setProperty("key", key);
    object->setProperty("installation_id", installation);
    return body;
}

LicenseManager::Reply request(const juce::String& action, const juce::var& body)
{
    // The key travels only in the JSON body, never in the URL or logs.
    int status = 0;
    auto stream = juce::URL(juce::String(STAG_ACTIVATION_URL) + "/" + action)
                      .withPOSTData(juce::JSON::toString(body, true))
                      .createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                                             .withHttpRequestCmd("POST")
                                             .withExtraHeaders("Content-Type: application/json\r\nAccept: application/json\r\n")
                                             .withConnectionTimeoutMs(15000)
                                             .withNumRedirectsToFollow(0)
                                             .withStatusCode(&status));
    return {status, stream != nullptr && status == 200 ? readJson(*stream, responseLimit) : juce::var{}};
}
} // namespace

juce::File LicenseManager::defaultFile()
{
#if JUCE_MAC
    const auto root = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                          .getChildFile("Library/Application Support");
#elif JUCE_LINUX
    const auto xdg = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", {});
    const auto root = xdg.startsWithChar('/') ? juce::File(xdg)
        : juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".local/share");
#else
    const auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#endif
    return root.getChildFile("Stag Audio/Motion Bands/activation-v2-" STAG_LICENSE_ENVIRONMENT ".json");
}

LicenseManager::LicenseManager(juce::File cache, Transport sender)
    : Thread("Motion Bands activation"), file(std::move(cache)), transport(sender ? std::move(sender) : request)
{
    refreshFromDisk();
    workerAvailable = startThread();
}

LicenseManager::~LicenseManager()
{
    stopThread(-1);
}

juce::String LicenseManager::message() const
{
    const juce::ScopedLock lock(mutex);
    return status;
}

void LicenseManager::setMessage(juce::String text)
{
    const juce::ScopedLock lock(mutex);
    status = std::move(text);
}

juce::var LicenseManager::read() const
{
    if (!file.existsAsFile())
        return {};
    juce::FileInputStream stream(file);
    return stream.openedOk() ? readJson(stream, recordLimit) : juce::var{};
}

bool LicenseManager::save(const juce::var& record) const
{
    const juce::TemporaryFile temporary(file, juce::TemporaryFile::useHiddenFile);
    const auto& destination = temporary.getFile();
#if !JUCE_WINDOWS
    // The file must be private even while the record is being written.
    int fd = ::open(destination.getFullPathName().toRawUTF8(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    ::close(fd);
#endif
    {
        juce::FileOutputStream stream(destination);
        const auto text = juce::JSON::toString(record, true);
        const auto size = text.getNumBytesAsUTF8();
        if (!stream.openedOk() || size > static_cast<size_t>(recordLimit) || !stream.write(text.toRawUTF8(), size))
            return false;
        stream.flush();
        if (stream.getStatus().failed())
            return false;
    }
#if JUCE_WINDOWS
    return MoveFileExW(destination.getFullPathName().toWideCharPointer(), file.getFullPathName().toWideCharPointer(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(destination.getFullPathName().toRawUTF8(), file.getFullPathName().toRawUTF8()) == 0;
#endif
}

void LicenseManager::refreshFromDisk()
{
    const juce::ScopedLock lock(mutex);
    if (!busy.load())
        active.store(isActiveRecord(read()));
}

bool LicenseManager::activate(const juce::String& key)
{
    auto trimmed = key.trim();
    if (!validKeyLength(trimmed))
    {
        setMessage("Paste the license key from your Stag Audio account.");
        return false;
    }
    return begin(true, std::move(trimmed));
}

bool LicenseManager::deactivate()
{
    return begin(false, {});
}

bool LicenseManager::begin(bool activation, juce::String key)
{
    {
        const juce::ScopedLock lock(mutex);
        if (busy.load())
            return false;
        if (!workerAvailable)
        {
            status = "Couldn't start the activation worker. Reopen your host and try again.";
            return false;
        }
        pendingActivation = activation;
        pendingKey = std::move(key);
        status = activation ? "Activating online..." : "Releasing this installation...";
        busy.store(true);
    }
    notify();
    return true;
}

void LicenseManager::run()
{
    for (;;)
    {
        if (!busy.load())
        {
            if (threadShouldExit())
                return;
            refreshFromDisk();
            wait(2000);
            continue;
        }

        juce::String key;
        bool activation;
        {
            const juce::ScopedLock lock(mutex);
            pendingKey.swapWith(key);
            activation = pendingActivation;
        }
        updateActivation(activation, key);
        busy.store(false);
    }
}

void LicenseManager::updateActivation(bool activation, const juce::String& key)
{
    if (file.getParentDirectory().createDirectory().failed())
    {
        setMessage("Couldn't create the local activation folder. Check your user-folder permissions.");
        return;
    }
    const ActivationLock lock(file.getSiblingFile(file.getFileName() + ".lock"));
    if (!lock)
    {
        setMessage("Couldn't lock activation. Check your user-folder permissions or try again in a moment.");
        return;
    }

    const auto record = read();
    const auto alreadyActive = isActiveRecord(record);
    active.store(alreadyActive);
    if (activation && !alreadyActive)
        activateOnline(key, record);
    else if (!activation && alreadyActive)
        deactivateOnline(record);
    else
        setMessage(alreadyActive ? "Activated. Ready for offline use." : "This installation is not activated.");
}

void LicenseManager::activateOnline(const juce::String& key, const juce::var& previous)
{
    const auto ours = recordMatchesBuild(previous);
    auto installation = ours ? stringField(previous, "installation_id") : juce::String{};
    if (!isUuid(installation))
        installation = juce::Uuid().toDashedString();

    // An unresolved attempt for the same key is retried under the same id, so a
    // timeout or 202 followed by a retry can never allocate a second slot.
    auto attempt = ours && stringField(previous, "key") == key ? stringField(previous, "pending_attempt_id")
                                                                : juce::String{};
    if (!isUuid(attempt))
        attempt = juce::Uuid().toDashedString();

    auto record = inactiveRecord(installation);
    auto* object = record.getDynamicObject();
    object->setProperty("key", key);
    object->setProperty("pending_attempt_id", attempt);
    if (!save(record))
    {
        setMessage("Couldn't save activation here. Check your user-folder permissions.");
        return;
    }

    auto body = requestBody(key, installation);
    body.getDynamicObject()->setProperty("attempt_id", attempt);
    body.getDynamicObject()->setProperty("label", juce::String("Motion Bands ") + platformName + " "
                                                      + installation.substring(0, 8));
    const auto reply = transport("activate", body);

    if (reply.status == 200 && isActivation(reply.json))
    {
        object->setProperty("activation_id", stringField(reply.json, "activation_id"));
        object->setProperty("pending_attempt_id", juce::var());
        object->setProperty("active", true);
        if (save(record))
        {
            active.store(true);
            setMessage("Activated. Ready for offline use.");
            return;
        }
        setMessage("Activated online but couldn't save it here. Check your user-folder permissions and activate "
                   "again; it won't use another computer slot.");
        return;
    }

    if (isDefinitiveDenial(reply.status))
    {
        // Nothing was allocated: forget the key and the settled attempt.
        save(inactiveRecord(installation));
        setMessage(activationOutcome(reply.status));
        return;
    }

    setMessage(reply.status == 200
                   ? "Activation returned an unexpected answer. Try again; it won't use another computer slot."
                   : activationOutcome(reply.status));
}

void LicenseManager::deactivateOnline(const juce::var& record)
{
    // Stage an inactive record with no key before releasing the slot. Once the
    // server confirms release, no second disk write is needed to stay inactive.
    const auto installation = stringField(record, "installation_id");
    if (!save(inactiveRecord(installation)))
    {
        setMessage("Couldn't update local activation. Check your user-folder permissions before releasing this installation.");
        return;
    }
    active.store(false);

    auto body = requestBody(stringField(record, "key"), installation);
    body.getDynamicObject()->setProperty("activation_id", stringField(record, "activation_id"));
    const auto reply = transport("deactivate", body);
    if (reply.status == 200 && stringField(reply.json, "status") == "deactivated")
    {
        setMessage("Installation released. You can activate another computer.");
        return;
    }
    if (save(record))
    {
        active.store(true);
        setMessage("Couldn't confirm slot release. Check your connection and try again. Your local activation is unchanged.");
        return;
    }
    setMessage("Couldn't confirm slot release or restore local activation. Check your user-folder permissions and contact support.");
}
