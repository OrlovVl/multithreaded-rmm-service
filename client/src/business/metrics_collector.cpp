#define NOMINMAX
#include "rmm/client/business/metrics_collector.hpp"

#include "rmm/shared/protocol.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <wbemidl.h>

#include <algorithm>
#include <comdef.h>
#include <sstream>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "wbemuuid.lib")

namespace rmm::client::business {

namespace {

std::string wideToUtf8(const std::wstring& ws)
{
    if (ws.empty()) return {};
    const int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()),
                                               nullptr, 0, nullptr, nullptr);
    std::string out(sizeNeeded, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()),
                        out.data(), sizeNeeded, nullptr, nullptr);
    return out;
}

double fileTimeToDouble(const FILETIME& ft)
{
    ULARGE_INTEGER ui{};
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    return static_cast<double>(ui.QuadPart);
}

// Умные указатели для COM с автоматическим вызовом Release
struct ComDeleter {
    void operator()(IUnknown* p) const { if (p) p->Release(); }
};
template<typename T> using ComPtr = std::unique_ptr<T, ComDeleter>;

// RAII для CoInitialize/CoUninitialize
struct ComScope {
    ComScope()  { CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComScope() { CoUninitialize(); }
};

// Подключение к WMI
ComPtr<IWbemServices> ConnectWMI()
{
    ComPtr<IWbemLocator> locator;
    IWbemLocator* rawLocator = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<LPVOID*>(&rawLocator));
    if (FAILED(hr) || !rawLocator)
        return nullptr;
    locator.reset(rawLocator);

    IWbemServices* rawServices = nullptr;
    BSTR ns = SysAllocString(L"ROOT\\CIMV2");
    hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &rawServices);
    SysFreeString(ns);
    if (FAILED(hr) || !rawServices)
        return nullptr;

    ComPtr<IWbemServices> services(rawServices);
    hr = CoSetProxyBlanket(services.get(),
                           RPC_C_AUTHN_WINNT,
                           RPC_C_AUTHZ_NONE,
                           nullptr,
                           RPC_C_AUTHN_LEVEL_CALL,
                           RPC_C_IMP_LEVEL_IMPERSONATE,
                           nullptr,
                           EOAC_NONE);
    if (FAILED(hr))
        return nullptr;

    return services;
}

// Выполнить WQL-запрос и вернуть первый объект
ComPtr<IWbemClassObject> QueryFirstObject(IWbemServices* services, const std::wstring& query)
{
    if (!services) return nullptr;

    IEnumWbemClassObject* rawEnumerator = nullptr;
    ComPtr<IEnumWbemClassObject> enumerator;
    BSTR qlang = SysAllocString(L"WQL");
    BSTR q = SysAllocString(query.c_str());
    HRESULT hr = services->ExecQuery(qlang, q,
                                     WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                     nullptr, &rawEnumerator);
    SysFreeString(qlang);
    SysFreeString(q);
    if (FAILED(hr) || !rawEnumerator)
        return nullptr;
    enumerator.reset(rawEnumerator);

    IWbemClassObject* obj = nullptr;
    ULONG returned = 0;
    hr = enumerator->Next(WBEM_INFINITE, 1, &obj, &returned);
    if (FAILED(hr) || returned == 0)
        return nullptr;

    return ComPtr<IWbemClassObject>(obj);
}

// Получить числовое свойство объекта
bool GetNumericProperty(IWbemClassObject* obj, const std::wstring& propName, double& out)
{
    VARIANT val;
    VariantInit(&val);
    HRESULT hr = obj->Get(propName.c_str(), 0, &val, nullptr, nullptr);
    if (FAILED(hr)) return false;

    bool success = false;
    if (val.vt == VT_I4 || val.vt == VT_UI4) {
        out = static_cast<double>(val.lVal);
        success = true;
    } else if (val.vt == VT_R8) {
        out = val.dblVal;
        success = true;
    }
    VariantClear(&val);
    return success;
}

bool GetBoolProperty(IWbemClassObject* obj, const std::wstring& propName, bool& out)
{
    VARIANT val;
    VariantInit(&val);
    HRESULT hr = obj->Get(propName.c_str(), 0, &val, nullptr, nullptr);
    if (FAILED(hr)) return false;

    bool success = false;
    if (val.vt == VT_BOOL) {
        out = (val.boolVal == VARIANT_TRUE);
        success = true;
    }
    VariantClear(&val);
    return success;
}

} // namespace

// ------------------------------------------------------------
// MetricsCollector implementation
// ------------------------------------------------------------
MetricsCollector::MetricsCollector() = default;

double MetricsCollector::collectCpuUsage()
{
    FILETIME idle{}, kernel{}, user{};
    if (!GetSystemTimes(&idle, &kernel, &user))
        return 0.0;

    const auto idleNow   = static_cast<unsigned long long>(fileTimeToDouble(idle));
    const auto kernelNow = static_cast<unsigned long long>(fileTimeToDouble(kernel));
    const auto userNow   = static_cast<unsigned long long>(fileTimeToDouble(user));

    if (m_firstCpuSample) {
        m_firstCpuSample = false;
        m_prevIdle   = idleNow;
        m_prevKernel = kernelNow;
        m_prevUser   = userNow;
        return 0.0;
    }

    const auto idleDelta   = idleNow - m_prevIdle;
    const auto kernelDelta = kernelNow - m_prevKernel;
    const auto userDelta   = userNow - m_prevUser;

    m_prevIdle   = idleNow;
    m_prevKernel = kernelNow;
    m_prevUser   = userNow;

    const auto total = kernelDelta + userDelta;
    if (total == 0) return 0.0;

    const double usage = 100.0 * (1.0 - static_cast<double>(idleDelta) / static_cast<double>(total));
    return std::clamp(usage, 0.0, 100.0);
}

double MetricsCollector::collectRamUsage()
{
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem))
        return 0.0;

    const double used = static_cast<double>(mem.ullTotalPhys - mem.ullAvailPhys);
    return mem.ullTotalPhys == 0 ? 0.0 : std::clamp(100.0 * used / static_cast<double>(mem.ullTotalPhys), 0.0, 100.0);
}

double MetricsCollector::collectDiskFreePercent()
{
    ULARGE_INTEGER freeBytes{}, totalBytes{}, totalFree{};
    if (!GetDiskFreeSpaceExW(L"C:\\", &freeBytes, &totalBytes, &totalFree))
        return 0.0;

    if (totalBytes.QuadPart == 0) return 0.0;
    return std::clamp(100.0 * static_cast<double>(freeBytes.QuadPart) / static_cast<double>(totalBytes.QuadPart), 0.0, 100.0);
}

double MetricsCollector::collectDiskFreeGb()
{
    ULARGE_INTEGER freeBytes{}, totalBytes{}, totalFree{};
    if (!GetDiskFreeSpaceExW(L"C:\\", &freeBytes, &totalBytes, &totalFree))
        return 0.0;

    return static_cast<double>(freeBytes.QuadPart) / (1024.0 * 1024.0 * 1024.0);
}

double MetricsCollector::collectTemperature()
{
    ComScope comGuard;
    auto services = ConnectWMI();
    if (!services) return 0.0;

    // Пробуем Win32_TemperatureProbe (более распространён)
    auto obj = QueryFirstObject(services.get(),
                                L"SELECT CurrentReading FROM Win32_TemperatureProbe WHERE CurrentReading IS NOT NULL");
    if (obj) {
        double temp = 0.0;
        if (GetNumericProperty(obj.get(), L"CurrentReading", temp)) {
            // CurrentReading обычно в десятых долях Цельсия или в градусах. Проверим диапазон.
            if (temp > 1000.0) temp /= 10.0; // иногда в десятых
            if (temp > 0.0 && temp < 150.0)
                return temp;
        }
    }

    // Пробуем MSAcpi_ThermalZoneTemperature
    obj = QueryFirstObject(services.get(),
                           L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature");
    if (obj) {
        double kelvin = 0.0;
        if (GetNumericProperty(obj.get(), L"CurrentTemperature", kelvin)) {
            // В Кельвинах * 10
            if (kelvin > 1000.0) {
                double celsius = (kelvin / 10.0) - 273.15;
                if (celsius > 0.0 && celsius < 150.0)
                    return celsius;
            }
        }
    }

    return 0.0;
}

bool MetricsCollector::collectSmartPredictFailure()
{
    ComScope comGuard;
    auto services = ConnectWMI();
    if (!services) return false;

    // Пробуем MSStorageDriver_FailurePredictStatus
    auto obj = QueryFirstObject(services.get(),
                                L"SELECT PredictFailure FROM MSStorageDriver_FailurePredictStatus");
    if (obj) {
        bool failure = false;
        if (GetBoolProperty(obj.get(), L"PredictFailure", failure))
            return failure;
    }

    // Резервный вариант: Win32_DiskDrive
    obj = QueryFirstObject(services.get(),
                           L"SELECT Status FROM Win32_DiskDrive WHERE Status IS NOT NULL");
    if (obj) {
        VARIANT val;
        VariantInit(&val);
        HRESULT hr = obj->Get(L"Status", 0, &val, nullptr, nullptr);
        if (SUCCEEDED(hr) && val.vt == VT_BSTR) {
            std::wstring status(val.bstrVal);
            VariantClear(&val);
            // Предсказание сбоя если статус не "OK"
            return status != L"OK";
        }
        VariantClear(&val);
    }

    return false;
}

std::vector<rmm::shared::ProcessInfo> MetricsCollector::collectProcesses()
{
    std::vector<rmm::shared::ProcessInfo> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            rmm::shared::ProcessInfo info;
            info.pid = static_cast<std::int32_t>(entry.th32ProcessID);
            info.name = wideToUtf8(entry.szExeFile);
            result.push_back(std::move(info));
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);

    // Ограничим количество процессов для экономии трафика
    if (result.size() > 50)
        result.resize(50);

    return result;
}

rmm::shared::MetricsSnapshot MetricsCollector::collect(const std::string& nodeName)
{
    rmm::shared::MetricsSnapshot snapshot;
    snapshot.nodeName = nodeName;
    snapshot.timestampUtc = rmm::shared::utcNowIso8601();
    snapshot.cpuUsage = collectCpuUsage();
    snapshot.ramUsage = collectRamUsage();
    snapshot.diskFreePercent = collectDiskFreePercent();
    snapshot.diskFreeGb = collectDiskFreeGb();
    snapshot.temperatureC = collectTemperature();
    snapshot.smartPredictFailure = collectSmartPredictFailure();
    snapshot.processes = collectProcesses();
    return snapshot;
}

} // namespace rmm::client::business