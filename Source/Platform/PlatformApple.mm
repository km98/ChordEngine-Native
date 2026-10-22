// macOS implementation of Platform.h (Foundation, CommonCrypto, Security).
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <CommonCrypto/CommonDigest.h>
#include <Security/SecRandom.h>
#include <unistd.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/stat.h>

#include "Platform.h"
#include "ObjCNames.h"

#include <chrono>
#include <cstdio>
#include <fstream>

#define ChordEngineNoRedirectDelegate CE_OBJC(ChordEngineNoRedirectDelegate)

// Refuses every redirect: the 3xx answer itself is returned to the caller.
@interface ChordEngineNoRedirectDelegate : NSObject <NSURLSessionTaskDelegate>
@end

@implementation ChordEngineNoRedirectDelegate
- (void)URLSession:(NSURLSession*)session
              task:(NSURLSessionTask*)task
willPerformHTTPRedirection:(NSHTTPURLResponse*)response
        newRequest:(NSURLRequest*)request
 completionHandler:(void (^)(NSURLRequest*))completionHandler
{
    (void) session; (void) task; (void) response; (void) request;
    completionHandler(nil);
}
@end

namespace chordengine::platform
{
std::int64_t currentTimeMillis() noexcept
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

double millisecondCounterHiRes() noexcept
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

std::string randomHex(std::size_t bytes)
{
    std::vector<std::uint8_t> buffer(bytes);
    if (bytes > 0 && SecRandomCopyBytes(kSecRandomDefault, bytes, buffer.data()) != errSecSuccess)
        arc4random_buf(buffer.data(), bytes);
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    for (const auto byte : buffer)
    {
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}

std::string sha256Hex(const void* data, std::size_t size)
{
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data, static_cast<CC_LONG>(size), digest);
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (const auto byte : digest)
    {
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}

std::string computerName()
{
    // The same value the 0.5 framework reported as the computer name (the host
    // name without ".local"), so a computer keeps its label on the licence
    // page across the 0.5 -> 0.6 update.
    char name[256] = {};
    if (gethostname(name, sizeof(name) - 1) != 0)
        return {};
    std::string text(name);
    const auto suffix = text.rfind(".local");
    if (suffix != std::string::npos)
        text.resize(suffix);
    return text;
}

std::string operatingSystemName()
{
    @autoreleasepool
    {
        // "Mac OSX " + the ProductVersion string, as the 0.5 framework reported it, so the
        // `platform` field the licence route stores keeps its format.
        NSDictionary* plist = [NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
        NSString* version = plist[@"ProductVersion"];
        if (version == nil)
        {
            const auto v = [[NSProcessInfo processInfo] operatingSystemVersion];
            version = [NSString stringWithFormat:@"%ld.%ld.%ld", (long) v.majorVersion, (long) v.minorVersion, (long) v.patchVersion];
        }
        return std::string("Mac OSX ") + version.UTF8String;
    }
}

std::filesystem::path userApplicationDataDirectory()
{
    @autoreleasepool
    {
        NSArray<NSString*>* paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
        if (paths.count > 0)
            return std::filesystem::path(paths.firstObject.UTF8String);
        const char* home = std::getenv("HOME");
        return std::filesystem::path(home != nullptr ? home : "/tmp") / "Library" / "Application Support";
    }
}

std::filesystem::path temporaryDirectory()
{
    std::error_code error;
    auto path = std::filesystem::temp_directory_path(error);
    return error ? std::filesystem::path("/tmp") : path;
}

std::optional<std::string> readFile(const std::filesystem::path& path, std::size_t maxBytes)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::string bytes;
    char buffer[16384];
    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0)
    {
        bytes.append(buffer, static_cast<std::size_t>(in.gcount()));
        if (bytes.size() > maxBytes)
            return std::nullopt;
    }
    return bytes;
}

bool writeFileAtomically(const std::filesystem::path& path, const std::string& bytes)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    auto temporary = path;
    temporary += ".tmp-" + randomHex(6);
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out)
        {
            out.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

std::int64_t fileModificationTimeMs(const std::filesystem::path& path)
{
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0)
        return 0;
    return static_cast<std::int64_t>(info.st_mtimespec.tv_sec) * 1000
         + static_cast<std::int64_t>(info.st_mtimespec.tv_nsec / 1000000);
}

// ---- HTTP -------------------------------------------------------------------
HttpResponse httpRequest(const HttpRequest& request)
{
    HttpResponse response;
    @autoreleasepool
    {
        NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:request.url.c_str()]];
        if (url == nil)
            return response;

        NSMutableURLRequest* urlRequest = [NSMutableURLRequest requestWithURL:url];
        urlRequest.HTTPMethod = [NSString stringWithUTF8String:request.method.c_str()];
        urlRequest.timeoutInterval = request.connectTimeoutMs / 1000.0;
        urlRequest.cachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        for (const auto& [name, value] : request.headers)
            [urlRequest setValue:[NSString stringWithUTF8String:value.c_str()]
              forHTTPHeaderField:[NSString stringWithUTF8String:name.c_str()]];
        if (!request.body.empty())
            urlRequest.HTTPBody = [NSData dataWithBytes:request.body.data() length:request.body.size()];

        NSURLSessionConfiguration* configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.timeoutIntervalForRequest = request.connectTimeoutMs / 1000.0;
        configuration.timeoutIntervalForResource = request.totalTimeoutMs / 1000.0;
        configuration.URLCache = nil;
        // Redirects: none when the caller forbids them; otherwise the system
        // default (which follows a bounded number), as before.
        NSURLSession* session = request.maxRedirects == 0
            ? [NSURLSession sessionWithConfiguration:configuration
                                            delegate:[[ChordEngineNoRedirectDelegate alloc] init]
                                       delegateQueue:nil]
            : [NSURLSession sessionWithConfiguration:configuration];

        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        __block NSData* data = nil;
        __block NSHTTPURLResponse* http = nil;
        __block NSError* failure = nil;
        NSURLSessionDataTask* task = [session dataTaskWithRequest:urlRequest
                                                completionHandler:^(NSData* d, NSURLResponse* r, NSError* e) {
            data = d;
            http = [r isKindOfClass:[NSHTTPURLResponse class]] ? (NSHTTPURLResponse*) r : nil;
            failure = e;
            dispatch_semaphore_signal(done);
        }];
        [task resume];
        const auto waitNs = static_cast<int64_t>(request.totalTimeoutMs + 2000) * NSEC_PER_MSEC;
        if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, waitNs)) != 0)
        {
            [task cancel];
            dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
            response.timedOut = true;
        }
        [session finishTasksAndInvalidate];

        if (failure != nil && failure.code == NSURLErrorTimedOut)
            response.timedOut = true;
        if (http != nil)
        {
            response.transportOk = true;
            response.status = static_cast<int>(http.statusCode);
            if (data != nil)
            {
                const auto length = static_cast<std::size_t>(data.length);
                const auto kept = std::min(length, request.maxResponseBytes);
                response.truncated = length > request.maxResponseBytes;
                response.body.assign(static_cast<const char*>(data.bytes), kept);
            }
        }
    }
    return response;
}

// ---- threads ------------------------------------------------------------------
void initialiseMainThread() {}

bool isMainThread() noexcept
{
    return pthread_main_np() != 0;
}

void callOnMainThread(std::function<void()> job)
{
    auto* heap = new std::function<void()>(std::move(job));
    dispatch_async_f(dispatch_get_main_queue(), heap, [](void* context)
    {
        auto* function = static_cast<std::function<void()>*>(context);
        (*function)();
        delete function;
    });
}

void runMainLoopFor(int ms)
{
    const auto until = millisecondCounterHiRes() + ms;
    while (millisecondCounterHiRes() < until)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, true);
}

struct RepeatingTimer::Impl
{
    dispatch_source_t source = nullptr;
    std::shared_ptr<std::function<void()>> callback;
    std::atomic<bool> running { false };
};

RepeatingTimer::RepeatingTimer() : impl_(std::make_unique<Impl>()) {}

RepeatingTimer::~RepeatingTimer()
{
    stop();
}

void RepeatingTimer::start(int intervalMs, std::function<void()> callback)
{
    stop();
    impl_->callback = std::make_shared<std::function<void()>>(std::move(callback));
    impl_->source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    const auto interval = static_cast<uint64_t>(intervalMs) * NSEC_PER_MSEC;
    dispatch_source_set_timer(impl_->source, dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(interval)),
                              interval, interval / 10);
    std::weak_ptr<std::function<void()>> weak = impl_->callback;
    dispatch_source_set_event_handler(impl_->source, ^{
        if (auto function = weak.lock())
            (*function)();
    });
    impl_->running = true;
    dispatch_resume(impl_->source);
}

void RepeatingTimer::stop()
{
    if (impl_->source != nullptr)
    {
        dispatch_source_cancel(impl_->source);
        impl_->source = nullptr;
    }
    impl_->callback.reset(); // a queued handler finds the callback gone
    impl_->running = false;
}

bool RepeatingTimer::isRunning() const noexcept
{
    return impl_->running;
}

// ---- desktop --------------------------------------------------------------------
bool openUrl(const std::string& url)
{
    @autoreleasepool
    {
        NSURL* target = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        return target != nil && [[NSWorkspace sharedWorkspace] openURL:target];
    }
}

bool revealInFileManager(const std::filesystem::path& path)
{
    @autoreleasepool
    {
        NSURL* target = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        if (target == nil)
            return false;
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ target ]];
        return true;
    }
}
} // namespace chordengine::platform
