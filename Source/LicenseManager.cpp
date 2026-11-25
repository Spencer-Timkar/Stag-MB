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
constexpr auto productSlug = "motion-bands";
constexpr int recordLimit = 16 * 1024;
constexpr int responseLimit = 32 * 1024;

constexpr bool catalogId(std::string_view id)
{
    if (id.empty() || id.front() == '0')
        return false;
    for (auto digit : id)
        if (digit < '0' || digit > '9')
            return false;
    return true;
}

static_assert(environment == "test" || environment == "live");
static_assert(catalogId(STAG_LICENSE_STORE_ID) && catalogId(STAG_LICENSE_PRODUCT_ID)
              && catalogId(STAG_LICENSE_VARIANT_ID));

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

bool matchesId(const juce::var& value, const char* expected)
{
    return (value.isString() || value.isInt() || value.isInt64()) && value.toString() == expected;
}

bool matchesCatalog(const juce::var& object)
{
    return matchesId(object["store_id"], STAG_LICENSE_STORE_ID)
        && matchesId(object["product_id"], STAG_LICENSE_PRODUCT_ID)
        && matchesId(object["variant_id"], STAG_LICENSE_VARIANT_ID);
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

bool recordMatchesBuild(const juce::var& record)
{
    return record["schema"].isInt() && static_cast<int>(record["schema"]) == 1
        && stringField(record, "product") == productSlug
        && stringField(record, "environment") == STAG_LICENSE_ENVIRONMENT
        && matchesCatalog(record);
}

bool validKeyLength(const juce::String& key)
{
    const auto length = key.length();
    return length >= 16 && length <= 128;
}

bool isActiveRecord(const juce::var& record)
{
    return recordMatchesBuild(record) && isTrue(record["active"])
        && validKeyLength(stringField(record, "key"))
        && stringField(record, "instance_id").isNotEmpty()
        && stringField(record, "installation_id").isNotEmpty();
}

bool isPerpetualThreeSlotKey(const juce::var& response)
{
    const auto& license = response["license_key"];
    const auto& limit = license["activation_limit"];
    return limit.isInt() && static_cast<int>(limit) == 3
        && license.hasProperty("expires_at") && license["expires_at"].isVoid();
}

juce::var inactiveRecord(const juce::String& installation)
{
    auto* object = new juce::DynamicObject();
    juce::var record(object);
    object->setProperty("schema", 1);
    object->setProperty("active", false);
    object->setProperty("product", productSlug);
    object->setProperty("environment", STAG_LICENSE_ENVIRONMENT);
    object->setProperty("store_id", STAG_LICENSE_STORE_ID);
    object->setProperty("product_id", STAG_LICENSE_PRODUCT_ID);
    object->setProperty("variant_id", STAG_LICENSE_VARIANT_ID);
    object->setProperty("installation_id", installation);
    return record;
}

LicenseManager::Reply request(const juce::String& action, const juce::StringPairArray& fields)
{
    // The key and instance are form fields, never URL query parameters or logs.
    auto url = juce::URL("https://api.lemonsqueezy.com/v1/licenses/" + action);
    for (int i = 0; i < fields.size(); ++i)
        url = url.withParameter(fields.getAllKeys()[i], fields.getAllValues()[i]);

    int status = 0;
    auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                                            .withHttpRequestCmd("POST")
                                            .withExtraHeaders("Accept: application/json\r\n")
                                            .withConnectionTimeoutMs(15000)
                                            .withNumRedirectsToFollow(0)
                                            .withStatusCode(&status));
    return {status, stream != nullptr && status == 200 ? readJson(*stream, responseLimit) : juce::var{}};
}

bool releaseSlot(const LicenseManager::Transport& transport, const juce::String& key,
                 const juce::String& instance)
{
    juce::StringPairArray fields;
    fields.set("license_key", key);
    fields.set("instance_id", instance);
    const auto reply = transport("deactivate", fields);
    return reply.status == 200 && isTrue(reply.json["deactivated"]) && matchesCatalog(reply.json["meta"]);
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
    return root.getChildFile("Stag Audio/Motion Bands/activation-" STAG_LICENSE_ENVIRONMENT ".json");
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
    juce::StringPairArray fields;
    fields.set("license_key", key);
    const auto validation = transport("validate", fields);
    if (validation.status != 200 || !isTrue(validation.json["valid"])
        || !matchesCatalog(validation.json["meta"]) || !isPerpetualThreeSlotKey(validation.json))
    {
        setMessage(validation.status == 0 || validation.status >= 500
                       ? "Couldn't reach activation. Check your connection and try again."
                       : "This key can't activate Stag Motion Bands. Check the key and your three-computer allowance.");
        return;
    }

    auto installation = recordMatchesBuild(previous) ? stringField(previous, "installation_id") : juce::String{};
    if (installation.isEmpty())
        installation = juce::Uuid().toString();
    auto record = inactiveRecord(installation);
    if (!save(record))
    {
        setMessage("Couldn't save activation here. Check your user-folder permissions.");
        return;
    }

    fields.set("instance_name", "Motion Bands " + installation.substring(0, 8));
    const auto reply = transport("activate", fields);
    const auto instance = stringField(reply.json["instance"], "id");
    const auto& activated = reply.json["activated"];
    const auto allocated = reply.status == 200 && isTrue(activated);
    if (!allocated || instance.isEmpty())
    {
        const auto explicitlyDenied = (reply.status >= 400 && reply.status < 500)
            || (reply.status == 200 && activated.isBool() && !allocated);
        setMessage(explicitlyDenied
                       ? "Couldn't activate this key. It may have reached its three-computer limit."
                       : "Activation didn't finish. If a slot was used, contact support before retrying.");
        return;
    }

    if (matchesCatalog(reply.json["meta"]) && isPerpetualThreeSlotKey(reply.json))
    {
        auto* object = record.getDynamicObject();
        object->setProperty("key", key);
        object->setProperty("instance_id", instance);
        object->setProperty("active", true);
        if (save(record))
        {
            active.store(true);
            setMessage("Activated. Ready for offline use.");
            return;
        }
    }

    setMessage(releaseSlot(transport, key, instance)
                   ? "Couldn't keep this activation. The slot was released; check the key and user-folder permissions."
                   : "Couldn't keep this activation or confirm its release. Contact support before retrying.");
}

void LicenseManager::deactivateOnline(const juce::var& record)
{
    // Stage an inactive record with no key before releasing the slot. Once the
    // provider confirms release, no second disk write is needed to stay inactive.
    if (!save(inactiveRecord(stringField(record, "installation_id"))))
    {
        setMessage("Couldn't update local activation. Check your user-folder permissions before releasing this installation.");
        return;
    }
    active.store(false);

    if (releaseSlot(transport, stringField(record, "key"), stringField(record, "instance_id")))
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
