#include "platform/windows/settings_io_lock.h"
#include <Aclapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

namespace clipss2::platform::windows
{
namespace
{
constexpr char domain[] = "clipss2.settings.io.user.v1";
#if defined(CLIPSS2_SETTINGS_TEST_NAMESPACE)
constexpr wchar_t namespace_prefix[] = L"Global\\clipss2.settings.test.user.v1.";
#else
constexpr wchar_t namespace_prefix[] = L"Global\\clipss2.settings.io.user.v1.";
#endif
constexpr DWORD mutex_access = SYNCHRONIZE | READ_CONTROL;
constexpr std::chrono::milliseconds wait_budget{250};
std::timed_mutex local_mutex;

[[noreturn]] void fail(const char* stage, DWORD error)
{
    throw std::runtime_error(std::string{"lock/"} + stage + ": Win32 error " + std::to_string(error));
}

struct CloseKernelHandle
{
    void operator()(HANDLE handle) const noexcept { (void)CloseHandle(handle); }
};
using UniqueHandle = std::unique_ptr<void, CloseKernelHandle>;

struct FreeLocal
{
    void operator()(void* memory) const noexcept { (void)LocalFree(memory); }
};

struct UserSid
{
    alignas(DWORD) std::array<BYTE, SECURITY_MAX_SID_SIZE> bytes{};
    DWORD length{};
};

UserSid read_user_sid()
{
    HANDLE raw{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) fail("token/open", GetLastError());
    const UniqueHandle token{raw};
    alignas(TOKEN_USER) std::array<BYTE, sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE> buffer{};
    DWORD used{};
    if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), static_cast<DWORD>(buffer.size()), &used))
        fail("token/user", GetLastError());
    if (used < sizeof(TOKEN_USER) || used > buffer.size()) fail("token/size", ERROR_INVALID_DATA);
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    const auto first = reinterpret_cast<std::uintptr_t>(buffer.data());
    const auto sid = reinterpret_cast<std::uintptr_t>(user->User.Sid);
    constexpr auto sid_header_size = offsetof(SID, SubAuthority);
    if (sid < first || sid - first > used - sid_header_size) fail("token/sid", ERROR_INVALID_SID);
    const auto* header = static_cast<const SID*>(user->User.Sid);
    const DWORD length = GetSidLengthRequired(header->SubAuthorityCount);
    if (length > SECURITY_MAX_SID_SIZE || length > used - (sid - first) || !IsValidSid(user->User.Sid))
        fail("token/sid", ERROR_INVALID_SID);
    UserSid result;
    std::copy_n(static_cast<const BYTE*>(user->User.Sid), length, result.bytes.begin());
    result.length = length;
    return result;
}

std::wstring mutex_name(const UserSid& user)
{
    std::array<BYTE, sizeof(domain) + SECURITY_MAX_SID_SIZE> input{};
    std::copy_n(domain, sizeof(domain), input.begin()); // Include the terminating NUL.
    std::copy_n(user.bytes.begin(), user.length, input.begin() + sizeof(domain));
    std::array<BYTE, 32> digest{};
    const NTSTATUS status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, input.data(),
        static_cast<ULONG>(sizeof(domain)) + user.length, digest.data(), static_cast<ULONG>(digest.size()));
    if (status != 0)
        throw std::runtime_error("lock/hash: NTSTATUS " + std::to_string(status));
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring name{namespace_prefix};
    for (const auto byte : digest)
    {
        name.push_back(hex[byte >> 4]);
        name.push_back(hex[byte & 0x0f]);
    }
    return name;
}

void verify_object(HANDLE handle, UserSid& user)
{
    PSID owner{};
    PACL acl{};
    PSECURITY_DESCRIPTOR descriptor{};
    const DWORD error = GetSecurityInfo(handle, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION |
        DACL_SECURITY_INFORMATION, &owner, nullptr, &acl, nullptr, &descriptor);
    const std::unique_ptr<void, FreeLocal> release_descriptor{descriptor};
    if (error != ERROR_SUCCESS) fail("inspect", error);
    if (!owner || !EqualSid(owner, user.bytes.data())) fail("owner", ERROR_ACCESS_DENIED);
    BOOL present{}, defaulted{};
    if (!GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted)) fail("inspect/dacl", GetLastError());
    if (!present || !acl || defaulted || acl->AceCount != 1) fail("dacl", ERROR_ACCESS_DENIED);
    void* entry{};
    if (!GetAce(acl, 0, &entry)) fail("inspect/ace", GetLastError());
    auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(entry);
    if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags != 0 ||
        ace->Header.AceSize != offsetof(ACCESS_ALLOWED_ACE, SidStart) + user.length ||
        ace->Mask != mutex_access || !EqualSid(&ace->SidStart, user.bytes.data()))
        fail("dacl/ace", ERROR_ACCESS_DENIED);
    DWORD flags{};
    if (!GetHandleInformation(handle, &flags)) fail("inspect/handle", GetLastError());
    if (flags & HANDLE_FLAG_INHERIT) fail("handle/inherit", ERROR_ACCESS_DENIED);
}

HANDLE open_verified_mutex()
{
    auto user = read_user_sid();
    const auto name = mutex_name(user);
    alignas(DWORD) std::array<BYTE, sizeof(ACL) + offsetof(ACCESS_ALLOWED_ACE, SidStart) +
        SECURITY_MAX_SID_SIZE> storage{};
    auto* acl = reinterpret_cast<ACL*>(storage.data());
    SECURITY_DESCRIPTOR descriptor{};
    if (!InitializeAcl(acl, static_cast<DWORD>(storage.size()), ACL_REVISION) ||
        !AddAccessAllowedAce(acl, ACL_REVISION, mutex_access, user.bytes.data()) ||
        !InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorOwner(&descriptor, user.bytes.data(), FALSE) ||
        !SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE))
        fail("descriptor", GetLastError());
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), &descriptor, FALSE};
    UniqueHandle handle{CreateMutexExW(&attributes, name.c_str(), 0, mutex_access)};
    if (!handle) fail("open", GetLastError());
    verify_object(handle.get(), user);
    return handle.release();
}
}

SettingsIoLock::SettingsIoLock()
    : handle_(open_verified_mutex()), local_(local_mutex, std::defer_lock)
{
}

void SettingsIoLock::acquire()
{
    const auto deadline = std::chrono::steady_clock::now() + wait_budget;
    switch (WaitForSingleObject(handle_, static_cast<DWORD>(wait_budget.count())))
    {
    case WAIT_OBJECT_0:
    case WAIT_ABANDONED:
        // The caller always reopens and validates the official settings bytes.
        // Any temp file from the former owner remains under that owner's name.
        owns_named_mutex_ = true;
        break;
    case WAIT_TIMEOUT:
        fail("wait", ERROR_TIMEOUT);
    case WAIT_FAILED:
        fail("wait", GetLastError());
    default:
        fail("wait/result", ERROR_INVALID_DATA);
    }
    if (std::chrono::steady_clock::now() >= deadline || !local_.try_lock_until(deadline))
        fail("local/wait", ERROR_TIMEOUT);
}

SettingsIoLock::~SettingsIoLock()
{
    if (local_.owns_lock()) local_.unlock();
    if (owns_named_mutex_ && !ReleaseMutex(handle_))
    {
        const DWORD error = GetLastError();
        std::fprintf(stderr, "Settings warning: lock/release: Win32 error %lu\n", error);
    }
    if (!CloseHandle(handle_))
    {
        const DWORD error = GetLastError();
        std::fprintf(stderr, "Settings warning: lock/close: Win32 error %lu\n", error);
    }
}
}
