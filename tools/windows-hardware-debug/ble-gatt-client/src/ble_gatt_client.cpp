// SPDX-License-Identifier: Apache-2.0

#include <windows.h>

#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace std::chrono_literals;
using winrt::Windows::Devices::Bluetooth::BluetoothAddressType;
using winrt::Windows::Devices::Bluetooth::BluetoothAdapter;
using winrt::Windows::Devices::Bluetooth::BluetoothCacheMode;
using winrt::Windows::Devices::Bluetooth::BluetoothConnectionStatus;
using winrt::Windows::Devices::Bluetooth::BluetoothLEDevice;
using winrt::Windows::Devices::Bluetooth::Advertisement::
  BluetoothLEAdvertisementWatcher;
using winrt::Windows::Devices::Bluetooth::Advertisement::
  BluetoothLEAdvertisementWatcherStatus;
using winrt::Windows::Devices::Bluetooth::Advertisement::
  BluetoothLEScanningMode;
using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::
  GattCharacteristicUuids;
using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::
  GattCharacteristic;
using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::
  GattCommunicationStatus;
using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::
  GattDescriptor;
using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::
  GattDescriptorUuids;
using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::
  GattDeviceService;
using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::
  GattSession;
using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::
  GattSessionStatus;
using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::
  GattServiceUuids;
using winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::
  GattWriteOption;
using winrt::Windows::Devices::Enumeration::DeviceAccessStatus;
using winrt::Windows::Devices::Enumeration::DeviceInformation;
using winrt::Windows::Foundation::AsyncStatus;
using winrt::Windows::Storage::Streams::DataReader;
using winrt::Windows::Storage::Streams::DataWriter;

constexpr unsigned int kDefaultScanTimeoutMs = 10000;
constexpr unsigned int kDefaultOperationTimeoutMs = 15000;
constexpr unsigned int kDefaultRediscoverTimeoutMs = 10000;
constexpr unsigned int kDefaultConnectAttempts = 3;
constexpr unsigned int kDefaultN13BurstCount = 100;
constexpr auto kConnectRetryDelay = 750ms;
constexpr auto kScanToConnectSettle = 1s;
constexpr std::size_t kN13FrameSize = 20;
constexpr std::size_t kN13CrcOffset = 16;
constexpr std::uint32_t kN13FrameMagic = 0x31474c42;
constexpr std::uint8_t kN13FrameVersion = 1;
constexpr std::uint8_t kN13EchoOpcode = 0x01;
constexpr std::uint8_t kN13BurstOpcode = 0x02;
constexpr std::uint8_t kN13ResponseBit = 0x80;
constexpr std::uint32_t kN13EchoSequence = 0x13000001;
constexpr std::uint32_t kN13EchoValue = 0x72581301;
constexpr std::uint32_t kN13BurstSequence = 0x13001000;
constexpr std::uint32_t kN13BurstValue = 0x72580000;
constexpr HRESULT kAttInvalidLength =
  E_BLUETOOTH_ATT_INVALID_ATTRIBUTE_VALUE_LENGTH;
constexpr HRESULT kAttUnlikely = E_BLUETOOTH_ATT_UNLIKELY;

const winrt::guid kN13ServiceUuid{
  0x72580001, 0x4e31, 0x3347,
  {0x41, 0x54, 0x54, 0x5f, 0x42, 0x4c, 0x45, 0x00}};
const winrt::guid kN13ControlUuid{
  0x72580002, 0x4e31, 0x3347,
  {0x41, 0x54, 0x54, 0x5f, 0x42, 0x4c, 0x45, 0x00}};
const winrt::guid kN13StatusUuid{
  0x72580003, 0x4e31, 0x3347,
  {0x41, 0x54, 0x54, 0x5f, 0x42, 0x4c, 0x45, 0x00}};

constexpr const char *kN13ServiceUuidText =
  "72580001-4e31-3347-4154-545f424c4500";
constexpr const char *kN13ControlUuidText =
  "72580002-4e31-3347-4154-545f424c4500";
constexpr const char *kN13StatusUuidText =
  "72580003-4e31-3347-4154-545f424c4500";

class tool_error : public std::runtime_error
{
public:
  tool_error(int exit_code, const std::string &message)
    : std::runtime_error(message), m_exit_code(exit_code)
  {
  }

  int exit_code() const noexcept
  {
    return m_exit_code;
  }

private:
  int m_exit_code;
};

struct options_s
{
  std::optional<std::uint64_t> address;
  std::string name;
  std::string expected_device_name;
  unsigned int scan_timeout_ms{kDefaultScanTimeoutMs};
  unsigned int operation_timeout_ms{kDefaultOperationTimeoutMs};
  unsigned int rediscover_timeout_ms{kDefaultRediscoverTimeoutMs};
  unsigned int connect_attempts{kDefaultConnectAttempts};
  unsigned int n13_burst_count{kDefaultN13BurstCount};
  std::filesystem::path result_file;
  bool probe_only{false};
  bool scan_only{false};
  bool rediscover{true};
  bool n13{false};
  bool n13_negative{false};
  bool n13_cached_discovery{false};
  bool n13_targeted_discovery{false};
};

struct scan_result_s
{
  std::uint64_t address{0};
  BluetoothAddressType address_type{BluetoothAddressType::Unspecified};
  std::string local_name;
  short rssi{0};
};

struct n13_result_s
{
  bool executed{false};
  std::uint16_t service_handle{0};
  std::uint16_t control_handle{0};
  std::uint16_t status_handle{0};
  std::uint16_t ccc_handle{0};
  unsigned int burst_requested{0};
  unsigned int burst_received{0};
  std::uint32_t first_sequence{0};
  std::uint32_t last_sequence{0};
  unsigned int duplicate_count{0};
  unsigned int lost_count{0};
  unsigned int crc_error_count{0};
  bool echo_read_matches{false};
  bool unsubscribe_quiet{false};
  bool negative_executed{false};
  unsigned int negative_rejected{0};
};

class gatt_session_lease
{
public:
  explicit gatt_session_lease(const GattSession &session)
    : m_session(session)
  {
    if (m_session && m_session.CanMaintainConnection())
      {
        m_session.MaintainConnection(true);
        m_maintaining = true;
      }
  }

  gatt_session_lease(const gatt_session_lease &) = delete;
  gatt_session_lease &operator=(const gatt_session_lease &) = delete;

  ~gatt_session_lease()
  {
    release();
  }

  bool maintaining() const noexcept
  {
    return m_maintaining;
  }

  void detach() noexcept
  {
    m_session = nullptr;
    m_maintaining = false;
  }

  void release() noexcept
  {
    if (!m_session)
      {
        return;
      }

    if (m_maintaining)
      {
        try
          {
            m_session.MaintainConnection(false);
          }
        catch (...)
          {
          }

        m_maintaining = false;
      }

    try
      {
        m_session.Close();
      }
    catch (...)
      {
      }

    m_session = nullptr;
  }

private:
  GattSession m_session{nullptr};
  bool m_maintaining{false};
};

std::string utf8_from_wide(const std::wstring &value)
{
  if (value.empty())
    {
      return {};
    }

  int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                 static_cast<int>(value.size()), nullptr, 0,
                                 nullptr, nullptr);
  if (size <= 0)
    {
      throw std::runtime_error("argument is not valid Unicode");
    }

  std::string result(static_cast<std::size_t>(size), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(), size,
                          nullptr, nullptr) != size)
    {
      throw std::runtime_error("failed to encode argument as UTF-8");
    }

  return result;
}

std::string json_escape(const std::string &value)
{
  std::ostringstream stream;
  for (unsigned char ch : value)
    {
      switch (ch)
        {
          case '\\':
            stream << "\\\\";
            break;
          case '"':
            stream << "\\\"";
            break;
          case '\b':
            stream << "\\b";
            break;
          case '\f':
            stream << "\\f";
            break;
          case '\n':
            stream << "\\n";
            break;
          case '\r':
            stream << "\\r";
            break;
          case '\t':
            stream << "\\t";
            break;
          default:
            if (ch < 0x20)
              {
                stream << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << static_cast<unsigned int>(ch)
                       << std::dec;
              }
            else
              {
                stream << static_cast<char>(ch);
              }
            break;
        }
    }

  return stream.str();
}

unsigned int parse_unsigned(const std::wstring &text, unsigned int maximum,
                            const char *option)
{
  std::size_t consumed = 0;
  unsigned long value = 0;

  try
    {
      value = std::stoul(text, &consumed, 0);
    }
  catch (const std::exception &)
    {
      throw std::runtime_error(std::string(option) + " expects an integer");
    }

  if (consumed != text.size() || value > maximum)
    {
      throw std::runtime_error(std::string(option) + " is out of range");
    }

  return static_cast<unsigned int>(value);
}

std::uint64_t parse_address(std::wstring text)
{
  text.erase(std::remove_if(text.begin(), text.end(), [](wchar_t ch) {
               return ch == L':' || ch == L'-' || ch == L' ';
             }),
             text.end());

  if (text.size() != 12)
    {
      throw std::runtime_error(
        "--address expects exactly six hexadecimal octets");
    }

  std::size_t consumed = 0;
  unsigned long long value = 0;
  try
    {
      value = std::stoull(text, &consumed, 16);
    }
  catch (const std::exception &)
    {
      throw std::runtime_error("--address contains a non-hex digit");
    }

  if (consumed != text.size() || value > 0xffffffffffffULL)
    {
      throw std::runtime_error("--address is invalid");
    }

  return static_cast<std::uint64_t>(value);
}

std::string format_address(std::uint64_t address)
{
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (int shift = 40; shift >= 0; shift -= 8)
    {
      if (shift != 40)
        {
          stream << ':';
        }

      stream << std::setw(2)
             << static_cast<unsigned int>((address >> shift) & 0xff);
    }

  return stream.str();
}

std::string bytes_to_hex(const std::vector<std::uint8_t> &bytes)
{
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (std::uint8_t byte : bytes)
    {
      stream << std::setw(2) << static_cast<unsigned int>(byte);
    }

  return stream.str();
}

const char *connection_status_name(BluetoothConnectionStatus status)
{
  return status == BluetoothConnectionStatus::Connected ? "Connected" :
                                                          "Disconnected";
}

const char *session_status_name(GattSessionStatus status)
{
  return status == GattSessionStatus::Active ? "Active" : "Closed";
}

const char *address_type_name(BluetoothAddressType type)
{
  switch (type)
    {
      case BluetoothAddressType::Public:
        return "Public";
      case BluetoothAddressType::Random:
        return "Random";
      case BluetoothAddressType::Unspecified:
      default:
        return "Unspecified";
    }
}

const char *gatt_status_name(GattCommunicationStatus status)
{
  switch (status)
    {
      case GattCommunicationStatus::Success:
        return "Success";
      case GattCommunicationStatus::Unreachable:
        return "Unreachable";
      case GattCommunicationStatus::ProtocolError:
        return "ProtocolError";
      case GattCommunicationStatus::AccessDenied:
        return "AccessDenied";
      default:
        return "Unknown";
    }
}

BluetoothCacheMode discovery_cache_mode(const options_s &options)
{
  return options.n13_cached_discovery ? BluetoothCacheMode::Cached :
                                        BluetoothCacheMode::Uncached;
}

const char *discovery_cache_name(const options_s &options)
{
  return options.n13_cached_discovery ? "cached" : "uncached";
}

const char *device_access_status_name(DeviceAccessStatus status)
{
  switch (status)
    {
      case DeviceAccessStatus::Allowed:
        return "Allowed";
      case DeviceAccessStatus::DeniedByUser:
        return "DeniedByUser";
      case DeviceAccessStatus::DeniedBySystem:
        return "DeniedBySystem";
      case DeviceAccessStatus::Unspecified:
      default:
        return "Unspecified";
    }
}

void print_usage()
{
  std::cout
    << "Windows BLE GATT client (bounded, no GUI)\n\n"
    << "Usage:\n"
    << "  ble_gatt_client.exe --address MAC [options]\n"
    << "  ble_gatt_client.exe --name NAME [options]\n"
    << "  ble_gatt_client.exe --probe\n\n"
    << "Options:\n"
    << "  --address MAC                 Exact six-octet BLE address\n"
    << "  --name NAME                   Exact advertised local name\n"
    << "  --expect-device-name NAME     Exact GAP Device Name value\n"
    << "  --n13                         Run the BK7258 N13 GATT data gate\n"
    << "  --n13-negative                Require four malformed writes to be rejected\n"
    << "  --n13-cached-discovery        Use cached enumeration for the negative gate\n"
    << "  --n13-targeted-discovery      Query only GAP/N13 UUIDs, uncached\n"
    << "  --n13-burst-count N           Notify frames, 1..100 (default 100)\n"
    << "  --scan-timeout-ms MS          Initial scan deadline (default 10000)\n"
    << "  --operation-timeout-ms MS     Per-WinRT-operation deadline (default 15000)\n"
    << "  --rediscover-timeout-ms MS    Post-close scan deadline (default 10000)\n"
    << "  --connect-attempts N          Fresh uncached connection attempts (default 3)\n"
    << "  --no-rediscover               Skip post-close advertising check\n"
    << "  --scan-only                   Stop after matching advertisement\n"
    << "  --result-file PATH            Write success JSON, replacing stale file\n"
    << "  --probe                       Check adapter central-role support only\n"
    << "  --help                        Show this help\n";
}

options_s parse_options(int argc, wchar_t **argv)
{
  options_s options;

  auto require_value = [&](int &index, const wchar_t *option) -> std::wstring {
    if (++index >= argc)
      {
        throw std::runtime_error(utf8_from_wide(option) +
                                 " requires a value");
      }

    return argv[index];
  };

  for (int index = 1; index < argc; ++index)
    {
      const std::wstring argument = argv[index];
      if (argument == L"--help" || argument == L"-h")
        {
          print_usage();
          std::exit(0);
        }
      else if (argument == L"--probe")
        {
          options.probe_only = true;
        }
      else if (argument == L"--scan-only")
        {
          options.scan_only = true;
        }
      else if (argument == L"--no-rediscover")
        {
          options.rediscover = false;
        }
      else if (argument == L"--n13")
        {
          options.n13 = true;
        }
      else if (argument == L"--n13-negative")
        {
          options.n13_negative = true;
        }
      else if (argument == L"--n13-cached-discovery")
        {
          options.n13_cached_discovery = true;
        }
      else if (argument == L"--n13-targeted-discovery")
        {
          options.n13_targeted_discovery = true;
        }
      else if (argument == L"--n13-burst-count")
        {
          options.n13_burst_count = parse_unsigned(
            require_value(index, L"--n13-burst-count"), 100,
            "--n13-burst-count");
        }
      else if (argument == L"--address")
        {
          options.address = parse_address(require_value(index, L"--address"));
        }
      else if (argument == L"--name")
        {
          options.name = utf8_from_wide(require_value(index, L"--name"));
        }
      else if (argument == L"--expect-device-name")
        {
          options.expected_device_name =
            utf8_from_wide(require_value(index, L"--expect-device-name"));
        }
      else if (argument == L"--scan-timeout-ms")
        {
          options.scan_timeout_ms =
            parse_unsigned(require_value(index, L"--scan-timeout-ms"),
                           120000, "--scan-timeout-ms");
        }
      else if (argument == L"--operation-timeout-ms")
        {
          options.operation_timeout_ms =
            parse_unsigned(require_value(index, L"--operation-timeout-ms"),
                           120000, "--operation-timeout-ms");
        }
      else if (argument == L"--rediscover-timeout-ms")
        {
          options.rediscover_timeout_ms = parse_unsigned(
            require_value(index, L"--rediscover-timeout-ms"), 120000,
            "--rediscover-timeout-ms");
        }
      else if (argument == L"--connect-attempts")
        {
          options.connect_attempts = parse_unsigned(
            require_value(index, L"--connect-attempts"), 10,
            "--connect-attempts");
        }
      else if (argument == L"--result-file")
        {
          options.result_file = require_value(index, L"--result-file");
        }
      else
        {
          throw std::runtime_error("unknown option: " +
                                   utf8_from_wide(argument));
        }
    }

  if (!options.probe_only && !options.address.has_value() &&
      options.name.empty())
    {
      throw std::runtime_error(
        "a target --address or --name is required unless --probe is used");
    }

  if (options.scan_timeout_ms == 0 || options.operation_timeout_ms == 0 ||
      options.connect_attempts == 0 ||
      (options.rediscover && options.rediscover_timeout_ms == 0))
    {
      throw std::runtime_error("timeouts must be greater than zero");
    }

  if (options.n13 && options.n13_burst_count == 0)
    {
      throw std::runtime_error("--n13-burst-count must be in range 1..100");
    }

  if (options.n13 && options.scan_only)
    {
      throw std::runtime_error("--n13 cannot be combined with --scan-only");
    }

  if (options.n13_negative && !options.n13)
    {
      throw std::runtime_error("--n13-negative requires --n13");
    }

  if (options.n13_cached_discovery && !options.n13_negative)
    {
      throw std::runtime_error(
        "--n13-cached-discovery requires --n13-negative");
    }

  if (options.n13_targeted_discovery && !options.n13)
    {
      throw std::runtime_error("--n13-targeted-discovery requires --n13");
    }

  if (options.n13_targeted_discovery && options.n13_cached_discovery)
    {
      throw std::runtime_error(
        "--n13-targeted-discovery conflicts with --n13-cached-discovery");
    }

  return options;
}

void clear_result_file_arguments(int argc, wchar_t **argv)
{
  for (int index = 1; index + 1 < argc; ++index)
    {
      if (std::wstring(argv[index]) == L"--result-file")
        {
          std::error_code error;
          std::filesystem::remove(argv[++index], error);
          if (error)
            {
              throw std::runtime_error("cannot remove stale result file");
            }
        }
    }
}

template<typename Async>
auto wait_for_async(const Async &operation, unsigned int timeout_ms,
                    const char *stage) -> decltype(operation.GetResults())
{
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (operation.Status() == AsyncStatus::Started &&
         std::chrono::steady_clock::now() < deadline)
    {
      std::this_thread::sleep_for(20ms);
    }

  if (operation.Status() == AsyncStatus::Started)
    {
      operation.Cancel();
      throw tool_error(8, std::string("timeout stage=") + stage);
    }

  if (operation.Status() == AsyncStatus::Canceled)
    {
      throw tool_error(8, std::string("cancelled stage=") + stage);
    }

  try
    {
      return operation.GetResults();
    }
  catch (const winrt::hresult_error &error)
    {
      std::ostringstream message;
      message << "WinRT failure stage=" << stage << " HRESULT=0x"
              << std::hex << std::setw(8) << std::setfill('0')
              << static_cast<std::uint32_t>(error.code().value)
              << " message=\"" << json_escape(winrt::to_string(error.message()))
              << "\"";
      throw tool_error(6, message.str());
    }
}

GattDeviceService open_cached_service_instance(
  const BluetoothLEDevice &device, const winrt::guid &uuid,
  const options_s &options, const char *role)
{
  const winrt::hstring selector =
    GattDeviceService::GetDeviceSelectorForBluetoothDeviceIdAndUuid(
      device.BluetoothDeviceId(), uuid, BluetoothCacheMode::Cached);
  const std::string query_stage = std::string(role) +
                                  "_service_instance_query";
  const auto instances = wait_for_async(
    DeviceInformation::FindAllAsync(selector), options.operation_timeout_ms,
    query_stage.c_str());
  if (instances.Size() > 1)
    {
      std::ostringstream message;
      message << "expected at most one cached " << role
              << " service instance for the selected device, got "
              << instances.Size();
      throw tool_error(7, message.str());
    }

  GattDeviceService service{nullptr};
  const char *source = nullptr;
  if (instances.Size() == 1)
    {
      const std::string open_stage = std::string(role) +
                                     "_service_instance_open";
      service = wait_for_async(
        GattDeviceService::FromIdAsync(instances.GetAt(0).Id()),
        options.operation_timeout_ms, open_stage.c_str());
      source = "device_uuid_selector";
    }
  else
    {
      /* Qualcomm's desktop stack can retain a usable per-UUID cache without
       * publishing an unpaired peer's GATT service as a PnP DeviceInformation
       * instance.  The deprecated synchronous projection is intentionally a
       * negative-gate-only last resort: it selects one UUID and never starts
       * the device-wide asynchronous enumeration that this mode must avoid.
       */

      try
        {
          service = device.GetGattService(uuid);
        }
      catch (const winrt::hresult_error &error)
        {
          if (error.code().value != HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
            {
              throw;
            }
        }

      source = "legacy_single_uuid_cache";
    }

  if (!service)
    {
      throw tool_error(7, std::string("cached ") + role +
                            " service instance open returned null");
    }

  if (service.Uuid() != uuid)
    {
      service.Close();
      throw tool_error(7, std::string("cached ") + role +
                            " service instance UUID mismatch");
    }

  std::cout << "BLEGATTC SERVICE_INSTANCE role=" << role
            << " source=" << source << " attribute_handle=0x"
            << std::hex << service.AttributeHandle() << std::dec << "\n";
  return service;
}

GattDeviceService open_targeted_uncached_service(
  const BluetoothLEDevice &device, const winrt::guid &uuid,
  const options_s &options, const char *role)
{
  const std::string stage = std::string(role) +
                            "_targeted_service_discovery";
  const auto result = wait_for_async(
    device.GetGattServicesForUuidAsync(uuid, BluetoothCacheMode::Uncached),
    options.operation_timeout_ms, stage.c_str());
  if (result.Status() != GattCommunicationStatus::Success)
    {
      throw tool_error(7, std::string(role) +
                            " targeted service discovery failed status=" +
                            gatt_status_name(result.Status()));
    }

  if (result.Services().Size() != 1)
    {
      std::ostringstream message;
      message << "expected exactly one uncached " << role
              << " service for the selected device, got "
              << result.Services().Size();
      throw tool_error(7, message.str());
    }

  GattDeviceService service = result.Services().GetAt(0);
  if (service.Uuid() != uuid)
    {
      service.Close();
      throw tool_error(7, std::string("uncached ") + role +
                            " service UUID mismatch");
    }

  std::cout << "BLEGATTC SERVICE_INSTANCE role=" << role
            << " source=targeted_uuid_uncached attribute_handle=0x"
            << std::hex << service.AttributeHandle() << std::dec << "\n";
  return service;
}

bool target_matches(const options_s &options, std::uint64_t address,
                    const std::string &local_name)
{
  if (options.address.has_value() && address != options.address.value())
    {
      return false;
    }

  if (!options.name.empty() && local_name != options.name)
    {
      return false;
    }

  return true;
}

scan_result_s scan_target(const options_s &options, unsigned int timeout_ms,
                          const char *stage)
{
  BluetoothLEAdvertisementWatcher watcher;
  watcher.ScanningMode(BluetoothLEScanningMode::Active);
  watcher.AllowExtendedAdvertisements(false);

  std::mutex lock;
  std::condition_variable event;
  std::optional<scan_result_s> found;

  auto received_revoker = watcher.Received(
    winrt::auto_revoke,
    [&](const BluetoothLEAdvertisementWatcher &, const auto &args) noexcept {
      try
        {
          const std::uint64_t address = args.BluetoothAddress();
          const std::string local_name =
            winrt::to_string(args.Advertisement().LocalName());
          if (!target_matches(options, address, local_name))
            {
              return;
            }

          std::lock_guard<std::mutex> guard(lock);
          if (!found.has_value())
            {
              found = scan_result_s{address, args.BluetoothAddressType(),
                                    local_name,
                                    args.RawSignalStrengthInDBm()};
              event.notify_one();
            }
        }
      catch (...)
        {
          /* Event callbacks must not throw across the WinRT boundary. */
        }
    });

  watcher.Start();
  {
    std::unique_lock<std::mutex> guard(lock);
    event.wait_for(guard, std::chrono::milliseconds(timeout_ms),
                   [&]() { return found.has_value(); });
  }
  watcher.Stop();

  const auto stop_deadline = std::chrono::steady_clock::now() + 2s;
  while (watcher.Status() != BluetoothLEAdvertisementWatcherStatus::Stopped &&
         watcher.Status() != BluetoothLEAdvertisementWatcherStatus::Aborted &&
         std::chrono::steady_clock::now() < stop_deadline)
    {
      std::this_thread::sleep_for(20ms);
    }

  if (!found.has_value())
    {
      throw tool_error(4, std::string("target not found stage=") + stage);
    }

  return found.value();
}

std::vector<std::uint8_t> read_buffer(
  const winrt::Windows::Storage::Streams::IBuffer &buffer)
{
  DataReader reader = DataReader::FromBuffer(buffer);
  std::vector<std::uint8_t> bytes(reader.UnconsumedBufferLength());
  reader.ReadBytes(bytes);
  return bytes;
}

std::uint16_t get_le16(const std::uint8_t *data)
{
  return static_cast<std::uint16_t>(data[0]) |
         static_cast<std::uint16_t>(data[1]) << 8;
}

std::uint32_t get_le32(const std::uint8_t *data)
{
  return static_cast<std::uint32_t>(data[0]) |
         static_cast<std::uint32_t>(data[1]) << 8 |
         static_cast<std::uint32_t>(data[2]) << 16 |
         static_cast<std::uint32_t>(data[3]) << 24;
}

void put_le16(std::uint8_t *data, std::uint16_t value)
{
  data[0] = static_cast<std::uint8_t>(value);
  data[1] = static_cast<std::uint8_t>(value >> 8);
}

void put_le32(std::uint8_t *data, std::uint32_t value)
{
  data[0] = static_cast<std::uint8_t>(value);
  data[1] = static_cast<std::uint8_t>(value >> 8);
  data[2] = static_cast<std::uint8_t>(value >> 16);
  data[3] = static_cast<std::uint8_t>(value >> 24);
}

std::uint32_t crc32_iso_hdlc(const std::uint8_t *data, std::size_t length)
{
  std::uint32_t crc = UINT32_MAX;
  for (std::size_t index = 0; index < length; ++index)
    {
      crc ^= data[index];
      for (unsigned int bit = 0; bit < 8; ++bit)
        {
          crc = (crc >> 1) ^
                (0xedb88320u &
                 static_cast<std::uint32_t>(-
                   static_cast<std::int32_t>(crc & 1u)));
        }
    }

  return crc ^ UINT32_MAX;
}

std::vector<std::uint8_t> make_n13_frame(std::uint8_t opcode,
                                         std::uint16_t count,
                                         std::uint32_t sequence,
                                         std::uint32_t value)
{
  std::vector<std::uint8_t> frame(kN13FrameSize, 0);
  put_le32(&frame[0], kN13FrameMagic);
  frame[4] = kN13FrameVersion;
  frame[5] = opcode;
  put_le16(&frame[6], count);
  put_le32(&frame[8], sequence);
  put_le32(&frame[12], value);
  put_le32(&frame[kN13CrcOffset],
           crc32_iso_hdlc(frame.data(), kN13CrcOffset));
  return frame;
}

struct decoded_n13_frame_s
{
  std::uint8_t opcode{0};
  std::uint16_t count{0};
  std::uint32_t sequence{0};
  std::uint32_t value{0};
};

decoded_n13_frame_s decode_n13_frame(
  const std::vector<std::uint8_t> &frame, const char *stage)
{
  if (frame.size() != kN13FrameSize)
    {
      throw tool_error(7, std::string(stage) +
                            " returned a non-20-byte N13 frame");
    }

  if (get_le32(&frame[0]) != kN13FrameMagic ||
      frame[4] != kN13FrameVersion)
    {
      throw tool_error(7, std::string(stage) +
                            " returned invalid N13 magic/version");
    }

  const std::uint32_t expected_crc = get_le32(&frame[kN13CrcOffset]);
  const std::uint32_t actual_crc =
    crc32_iso_hdlc(frame.data(), kN13CrcOffset);
  if (expected_crc != actual_crc)
    {
      throw tool_error(7, std::string(stage) +
                            " returned an invalid N13 CRC");
    }

  return decoded_n13_frame_s{frame[5], get_le16(&frame[6]),
                             get_le32(&frame[8]), get_le32(&frame[12])};
}

winrt::Windows::Storage::Streams::IBuffer make_buffer(
  const std::vector<std::uint8_t> &bytes)
{
  DataWriter writer;
  writer.WriteBytes(bytes);
  return writer.DetachBuffer();
}

template<typename Characteristic>
std::vector<std::uint8_t> read_characteristic(
  const Characteristic &characteristic, const options_s &options,
  const char *stage)
{
  auto result = wait_for_async(
    characteristic.ReadValueAsync(BluetoothCacheMode::Uncached),
    options.operation_timeout_ms, stage);
  if (result.Status() != GattCommunicationStatus::Success)
    {
      throw tool_error(7, std::string(stage) + " failed status=" +
                            gatt_status_name(result.Status()));
    }

  return read_buffer(result.Value());
}

template<typename Characteristic>
void write_characteristic(const Characteristic &characteristic,
                          const std::vector<std::uint8_t> &value,
                          const options_s &options, const char *stage)
{
  auto status = wait_for_async(
    characteristic.WriteValueAsync(make_buffer(value),
                                    GattWriteOption::WriteWithResponse),
    options.operation_timeout_ms, stage);
  if (status != GattCommunicationStatus::Success)
    {
      throw tool_error(7, std::string(stage) + " failed status=" +
                            gatt_status_name(status));
    }
}

template<typename Characteristic>
void expect_characteristic_write_rejected(
  const Characteristic &characteristic,
  const std::vector<std::uint8_t> &value,
  const options_s &options, const char *stage, const char *case_name,
  HRESULT expected_hresult)
{
  const auto operation = characteristic.WriteValueAsync(
    make_buffer(value), GattWriteOption::WriteWithResponse);
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::milliseconds(options.operation_timeout_ms);
  while (operation.Status() == AsyncStatus::Started &&
         std::chrono::steady_clock::now() < deadline)
    {
      std::this_thread::sleep_for(20ms);
    }

  if (operation.Status() == AsyncStatus::Started)
    {
      operation.Cancel();
      throw tool_error(8, std::string("timeout stage=") + stage);
    }

  if (operation.Status() == AsyncStatus::Canceled)
    {
      throw tool_error(8, std::string("cancelled stage=") + stage);
    }

  try
    {
      const auto status = operation.GetResults();
      if (status != GattCommunicationStatus::ProtocolError)
        {
          throw tool_error(
            7, std::string("N13 negative write was not rejected case=") +
                 case_name + " status=" + gatt_status_name(status));
        }

      std::cout << "BLEGATTC N13_NEGATIVE case=" << case_name
                << " status=ProtocolError projection=status rejected=1\n";
      return;
    }
  catch (const winrt::hresult_error &error)
    {
      if (error.code().value != expected_hresult)
        {
          std::ostringstream message;
          message << "N13 negative write returned wrong ATT HRESULT case="
                  << case_name << " expected=0x" << std::hex
                  << static_cast<std::uint32_t>(expected_hresult)
                  << " actual=0x"
                  << static_cast<std::uint32_t>(error.code().value);
          throw tool_error(7, message.str());
        }

      std::cout << "BLEGATTC N13_NEGATIVE case=" << case_name
                << " status=ProtocolError projection=hresult hresult=0x"
                << std::hex
                << static_cast<std::uint32_t>(error.code().value)
                << std::dec << " rejected=1\n";
    }
}

void request_service_access(const GattDeviceService &service,
                            const options_s &options, const char *role,
                            const char *stage)
{
  const DeviceAccessStatus access = wait_for_async(
    service.RequestAccessAsync(), options.operation_timeout_ms, stage);
  std::cout << "BLEGATTC SERVICE_ACCESS role=" << role
            << " status=" << device_access_status_name(access) << "\n";
  if (access != DeviceAccessStatus::Allowed)
    {
      throw tool_error(6, std::string(role) +
                            " service access status=" +
                            device_access_status_name(access));
    }
}

n13_result_s run_n13_gate(const GattDeviceService &service,
                          const options_s &options)
{
  n13_result_s result;
  request_service_access(service, options, "n13", "n13_service_access");
  auto characteristics_result = wait_for_async(
    service.GetCharacteristicsAsync(discovery_cache_mode(options)),
    options.operation_timeout_ms, "n13_characteristic_discovery");
  if (characteristics_result.Status() != GattCommunicationStatus::Success)
    {
      throw tool_error(
        7, std::string("N13 characteristic discovery failed status=") +
             gatt_status_name(characteristics_result.Status()));
    }

  GattCharacteristic control{nullptr};
  GattCharacteristic status{nullptr};
  unsigned int control_count = 0;
  unsigned int status_count = 0;
  for (const auto &characteristic : characteristics_result.Characteristics())
    {
      if (characteristic.Uuid() == kN13ControlUuid)
        {
          control = characteristic;
          ++control_count;
        }
      else if (characteristic.Uuid() == kN13StatusUuid)
        {
          status = characteristic;
          ++status_count;
        }
    }

  if (control_count != 1 || status_count != 1)
    {
      throw tool_error(7,
                       "expected exactly one N13 control and status "
                       "characteristic");
    }

  auto descriptors_result = wait_for_async(
    status.GetDescriptorsAsync(discovery_cache_mode(options)),
    options.operation_timeout_ms, "n13_ccc_discovery");
  if (descriptors_result.Status() != GattCommunicationStatus::Success)
    {
      throw tool_error(7, std::string("N13 descriptor discovery failed status=") +
                            gatt_status_name(descriptors_result.Status()));
    }

  GattDescriptor ccc{nullptr};
  unsigned int ccc_count = 0;
  for (const auto &descriptor : descriptors_result.Descriptors())
    {
      if (descriptor.Uuid() ==
          GattDescriptorUuids::ClientCharacteristicConfiguration())
        {
          ccc = descriptor;
          ++ccc_count;
        }
    }

  if (ccc_count != 1)
    {
      throw tool_error(7, "expected exactly one N13 CCC descriptor");
    }
  result.executed = true;
  result.service_handle = service.AttributeHandle();
  result.control_handle = control.AttributeHandle();
  result.status_handle = status.AttributeHandle();
  result.ccc_handle = ccc.AttributeHandle();
  result.burst_requested = options.n13_burst_count;

  std::cout << "BLEGATTC N13_DISCOVER service=" << kN13ServiceUuidText
            << " service_handle=0x" << std::hex << result.service_handle
            << " control=" << kN13ControlUuidText
            << " control_handle=0x" << result.control_handle
            << " status=" << kN13StatusUuidText
            << " status_handle=0x" << result.status_handle
            << " ccc_handle=0x" << result.ccc_handle << std::dec
            << " cache=" << discovery_cache_name(options)
            << "\n";

  const auto initial_value =
    read_characteristic(status, options, "n13_initial_status_read");
  const auto initial = decode_n13_frame(initial_value, "n13_initial_status");
  std::cout << "BLEGATTC N13_READ stage=initial value_hex="
            << bytes_to_hex(initial_value) << " opcode="
            << static_cast<unsigned int>(initial.opcode)
            << " count=" << initial.count
            << " sequence=" << initial.sequence
            << " value=" << initial.value << "\n";

  if (options.n13_negative)
    {
      const auto valid = make_n13_frame(
        kN13EchoOpcode, 1, kN13EchoSequence + 2, kN13EchoValue + 2);

      auto bad_length = valid;
      bad_length.pop_back();
      expect_characteristic_write_rejected(
        control, bad_length, options, "n13_negative_length", "length",
        kAttInvalidLength);

      auto bad_magic = valid;
      bad_magic[0] ^= 0x01;
      put_le32(&bad_magic[kN13CrcOffset],
               crc32_iso_hdlc(bad_magic.data(), kN13CrcOffset));
      expect_characteristic_write_rejected(
        control, bad_magic, options, "n13_negative_magic", "magic",
        kAttUnlikely);

      auto bad_version = valid;
      bad_version[4] = kN13FrameVersion + 1;
      put_le32(&bad_version[kN13CrcOffset],
               crc32_iso_hdlc(bad_version.data(), kN13CrcOffset));
      expect_characteristic_write_rejected(
        control, bad_version, options, "n13_negative_version", "version",
        kAttUnlikely);

      auto bad_crc = valid;
      bad_crc[kN13CrcOffset] ^= 0x01;
      expect_characteristic_write_rejected(
        control, bad_crc, options, "n13_negative_crc", "crc",
        kAttUnlikely);

      result.negative_executed = true;
      result.negative_rejected = 4;
      std::cout << "BLEGATTC N13_NEGATIVE_RESULT requested=4 rejected=4"
                   " link_probe=pending\n";
    }

  std::mutex notification_lock;
  std::condition_variable notification_event;
  std::vector<std::vector<std::uint8_t>> notifications;
  bool callback_failed = false;
  auto notification_revoker = status.ValueChanged(
    winrt::auto_revoke,
    [&](const auto &, const auto &args) noexcept {
      try
        {
          auto value = read_buffer(args.CharacteristicValue());
          std::lock_guard<std::mutex> guard(notification_lock);
          notifications.push_back(std::move(value));
          notification_event.notify_one();
        }
      catch (...)
        {
          std::lock_guard<std::mutex> guard(notification_lock);
          callback_failed = true;
          notification_event.notify_one();
        }
    });

  auto ccc_status = wait_for_async(
    ccc.WriteValueAsync(make_buffer({0x01, 0x00})),
    options.operation_timeout_ms, "n13_subscribe");
  if (ccc_status != GattCommunicationStatus::Success)
    {
      throw tool_error(7, std::string("N13 subscribe failed status=") +
                            gatt_status_name(ccc_status));
    }

  auto collect_exact = [&](std::size_t expected, const char *stage) {
    std::unique_lock<std::mutex> guard(notification_lock);
    const bool ready = notification_event.wait_for(
      guard, std::chrono::milliseconds(options.operation_timeout_ms), [&]() {
        return callback_failed || notifications.size() >= expected;
      });
    if (!ready)
      {
        throw tool_error(8, std::string("timeout stage=") + stage);
      }

    if (callback_failed)
      {
        throw tool_error(7, std::string(stage) +
                              " notification callback failed");
      }

    notification_event.wait_for(guard, 250ms, [&]() {
      return callback_failed || notifications.size() > expected;
    });
    if (callback_failed || notifications.size() != expected)
      {
        throw tool_error(7, std::string(stage) +
                              " received an unexpected notification count");
      }

    return notifications;
  };

  const auto echo_request = make_n13_frame(
    kN13EchoOpcode, 1, kN13EchoSequence, kN13EchoValue);
  {
    std::lock_guard<std::mutex> guard(notification_lock);
    notifications.clear();
  }
  write_characteristic(control, echo_request, options, "n13_echo_write");
  auto echo_notifications = collect_exact(1, "n13_echo_notify");
  const auto echo = decode_n13_frame(echo_notifications[0], "n13_echo_notify");
  if (echo.opcode != (kN13EchoOpcode | kN13ResponseBit) ||
      echo.count != 1 || echo.sequence != kN13EchoSequence ||
      echo.value != kN13EchoValue)
    {
      throw tool_error(7, "N13 echo notification did not match request");
    }

  const auto echo_read_value =
    read_characteristic(status, options, "n13_echo_status_read");
  result.echo_read_matches = echo_read_value == echo_notifications[0];
  if (!result.echo_read_matches)
    {
      throw tool_error(7, "N13 echo status read did not match notification");
    }

  std::cout << "BLEGATTC N13_ECHO sequence=" << echo.sequence
            << " value=" << echo.value << " crc_ok=1 read_match=1\n";
  if (result.negative_executed)
    {
      std::cout << "BLEGATTC N13_NEGATIVE_LINK usable=1"
                   " valid_echo_after_reject=1\n";
    }

  const auto burst_request = make_n13_frame(
    kN13BurstOpcode,
    static_cast<std::uint16_t>(options.n13_burst_count),
    kN13BurstSequence, kN13BurstValue);
  {
    std::lock_guard<std::mutex> guard(notification_lock);
    notifications.clear();
  }
  write_characteristic(control, burst_request, options, "n13_burst_write");
  auto burst_notifications = collect_exact(options.n13_burst_count,
                                            "n13_burst_notify");
  for (std::size_t index = 0; index < burst_notifications.size(); ++index)
    {
      const auto frame = decode_n13_frame(burst_notifications[index],
                                          "n13_burst_notify");
      const std::uint32_t expected_sequence =
        kN13BurstSequence + static_cast<std::uint32_t>(index);
      const std::uint32_t expected_value =
        kN13BurstValue + static_cast<std::uint32_t>(index);
      if (frame.opcode != (kN13BurstOpcode | kN13ResponseBit) ||
          frame.count != options.n13_burst_count ||
          frame.sequence != expected_sequence ||
          frame.value != expected_value)
        {
          throw tool_error(7, "N13 burst sequence/value mismatch at index=" +
                                std::to_string(index));
        }
    }

  result.burst_received =
    static_cast<unsigned int>(burst_notifications.size());
  result.first_sequence = kN13BurstSequence;
  result.last_sequence =
    kN13BurstSequence + options.n13_burst_count - 1;
  std::cout << "BLEGATTC N13_BURST requested=" << result.burst_requested
            << " received=" << result.burst_received
            << " first_sequence=" << result.first_sequence
            << " last_sequence=" << result.last_sequence
            << " crc_errors=0 lost=0 duplicate=0\n";

  ccc_status = wait_for_async(
    ccc.WriteValueAsync(make_buffer({0x00, 0x00})),
    options.operation_timeout_ms, "n13_unsubscribe");
  if (ccc_status != GattCommunicationStatus::Success)
    {
      throw tool_error(7, std::string("N13 unsubscribe failed status=") +
                            gatt_status_name(ccc_status));
    }

  {
    std::lock_guard<std::mutex> guard(notification_lock);
    notifications.clear();
  }
  const auto quiet_request = make_n13_frame(
    kN13EchoOpcode, 1, kN13EchoSequence + 1, kN13EchoValue + 1);
  write_characteristic(control, quiet_request, options,
                       "n13_unsubscribed_echo_write");
  {
    std::unique_lock<std::mutex> guard(notification_lock);
    const bool unexpected = notification_event.wait_for(
      guard, 500ms, [&]() {
        return callback_failed || !notifications.empty();
      });
    if (callback_failed || unexpected)
      {
        throw tool_error(7, "N13 notification arrived after unsubscribe");
      }
  }

  const auto quiet_read =
    read_characteristic(status, options, "n13_unsubscribed_status_read");
  const auto quiet = decode_n13_frame(quiet_read, "n13_unsubscribed_status");
  result.unsubscribe_quiet =
    quiet.opcode == (kN13EchoOpcode | kN13ResponseBit) &&
    quiet.count == 1 && quiet.sequence == kN13EchoSequence + 1 &&
    quiet.value == kN13EchoValue + 1;
  if (!result.unsubscribe_quiet)
    {
      throw tool_error(7, "N13 link/status failed after unsubscribe");
    }

  std::cout << "BLEGATTC N13_UNSUBSCRIBE quiet=1 link_usable=1\n";
  service.Close();
  return result;
}

void write_result_file(const options_s &options, const scan_result_s &scan,
                       const std::string &device_name, bool rediscovered,
                       const n13_result_s &n13 = {})
{
  if (options.result_file.empty())
    {
      return;
    }

  std::error_code error;
  const auto parent = options.result_file.parent_path();
  if (!parent.empty())
    {
      std::filesystem::create_directories(parent, error);
      if (error)
        {
          throw std::runtime_error("cannot create result-file directory");
        }
    }

  std::ofstream output(options.result_file,
                       std::ios::binary | std::ios::trunc);
  if (!output)
    {
      throw std::runtime_error("cannot create result file");
    }

  output << "{\n"
         << "  \"status\": \"passed\",\n"
         << "  \"address\": \"" << format_address(scan.address)
         << "\",\n"
         << "  \"address_type\": \""
         << address_type_name(scan.address_type) << "\",\n"
         << "  \"advertised_name\": \"" << json_escape(scan.local_name)
         << "\",\n"
         << "  \"rssi_dbm\": " << scan.rssi << ",\n"
         << "  \"discovery_cache\": \""
         << discovery_cache_name(options) << "\",\n"
         << "  \"gap_service_uuid\": "
            "\"00001800-0000-1000-8000-00805f9b34fb\",\n"
         << "  \"device_name_uuid\": "
            "\"00002a00-0000-1000-8000-00805f9b34fb\",\n"
         << "  \"device_name\": \"" << json_escape(device_name)
         << "\",\n"
         << "  \"rediscovered\": " << (rediscovered ? "true" : "false")
         << ",\n"
         << "  \"n13\": ";
  if (!n13.executed)
    {
      output << "null\n";
    }
  else
    {
      output
        << "{\n"
        << "    \"service_uuid\": \"" << kN13ServiceUuidText << "\",\n"
        << "    \"control_uuid\": \"" << kN13ControlUuidText << "\",\n"
        << "    \"status_uuid\": \"" << kN13StatusUuidText << "\",\n"
        << "    \"service_handle\": " << n13.service_handle << ",\n"
        << "    \"control_handle\": " << n13.control_handle << ",\n"
        << "    \"status_handle\": " << n13.status_handle << ",\n"
        << "    \"ccc_handle\": " << n13.ccc_handle << ",\n"
        << "    \"echo_read_matches\": "
        << (n13.echo_read_matches ? "true" : "false") << ",\n"
        << "    \"negative_executed\": "
        << (n13.negative_executed ? "true" : "false") << ",\n"
        << "    \"negative_rejected\": " << n13.negative_rejected
        << ",\n"
        << "    \"burst_requested\": " << n13.burst_requested << ",\n"
        << "    \"burst_received\": " << n13.burst_received << ",\n"
        << "    \"first_sequence\": " << n13.first_sequence << ",\n"
        << "    \"last_sequence\": " << n13.last_sequence << ",\n"
        << "    \"crc_errors\": " << n13.crc_error_count << ",\n"
        << "    \"lost\": " << n13.lost_count << ",\n"
        << "    \"duplicates\": " << n13.duplicate_count << ",\n"
        << "    \"unsubscribe_quiet\": "
        << (n13.unsubscribe_quiet ? "true" : "false") << "\n"
        << "  }\n";
    }

  output << "}\n";
  output.flush();
  if (!output)
    {
      throw std::runtime_error("cannot finalize result file");
    }
}

int run(const options_s &options)
{
  winrt::init_apartment(winrt::apartment_type::multi_threaded);

  BluetoothAdapter adapter = wait_for_async(
    BluetoothAdapter::GetDefaultAsync(), options.operation_timeout_ms,
    "adapter");
  if (!adapter)
    {
      throw tool_error(3, "no default Bluetooth adapter");
    }

  const bool low_energy = adapter.IsLowEnergySupported();
  const bool central = adapter.IsCentralRoleSupported();
  std::cout << "BLEGATTC ADAPTER address="
            << format_address(adapter.BluetoothAddress())
            << " low_energy=" << (low_energy ? 1 : 0)
            << " central=" << (central ? 1 : 0) << "\n";
  if (!low_energy || !central)
    {
      throw tool_error(3, "adapter lacks the BLE central role");
    }

  if (options.probe_only)
    {
      std::cout << "BLEGATTC RESULT PASS probe_only=1\n";
      return 0;
    }

  const scan_result_s scan =
    scan_target(options, options.scan_timeout_ms, "initial_scan");
  std::cout << "BLEGATTC SCAN address=" << format_address(scan.address)
            << " address_type=" << address_type_name(scan.address_type)
            << " name=\"" << json_escape(scan.local_name) << "\""
            << " rssi=" << scan.rssi << "\n";

  if (options.scan_only)
    {
      write_result_file(options, scan, "", false);
      std::cout << "BLEGATTC RESULT PASS scan_only=1\n";
      return 0;
    }

  BluetoothLEDevice address_device = wait_for_async(
    BluetoothLEDevice::FromBluetoothAddressAsync(scan.address,
                                                 scan.address_type),
    options.operation_timeout_ms, "device_lookup");
  if (!address_device)
    {
      throw tool_error(5, "device lookup returned null after scan");
    }

  const auto device_information = address_device.DeviceInformation();
  const winrt::hstring association_id_value = device_information.Id();
  const std::string association_id =
    winrt::to_string(association_id_value);
  const bool paired = device_information.Pairing().IsPaired();
  const DeviceAccessStatus diagnostic_access =
    address_device.DeviceAccessInformation().CurrentStatus();
  std::cout << "BLEGATTC AEP id=\"" << json_escape(association_id)
            << "\" paired=" << (paired ? 1 : 0)
            << " access=" << device_access_status_name(diagnostic_access)
            << "\n";

  /* Keep the DeviceInformation/AEP read above only as a permissions
   * diagnostic, then close it.  A fresh device object is created for every
   * bounded connection attempt below.
   */

  address_device.Close();
  address_device = nullptr;
  BluetoothLEDevice device{nullptr};
  GattSession session{nullptr};
  std::vector<GattDeviceService> services;
  unsigned int connected_attempt = 0;

  for (unsigned int attempt = 1; attempt <= options.connect_attempts; ++attempt)
    {
      if (attempt > 1)
        {
          const scan_result_s retry_scan = scan_target(
            options, options.scan_timeout_ms, "connection_retry_scan");
          std::cout << "BLEGATTC RETRY_SCAN attempt=" << attempt
                    << " address=" << format_address(retry_scan.address)
                    << " address_type="
                    << address_type_name(retry_scan.address_type)
                    << " rssi=" << retry_scan.rssi << "\n";
        }

      /* Some Windows controllers report the advertisement watcher Stopped
       * before the radio has completed its scan-to-initiator transition.
       * Keep this short and bounded, but do not race object creation against
       * that hardware transition.
       */

      std::this_thread::sleep_for(kScanToConnectSettle);

      /* Address resolution is the board-proven first path.  Retain the AEP ID
       * as a fresh fallback for drivers whose address lookup returns
       * Unreachable before ATT discovery.
       */

      const bool use_association_id = (attempt % 2) == 0;
      if (use_association_id)
        {
          device = wait_for_async(
            BluetoothLEDevice::FromIdAsync(association_id_value),
            options.operation_timeout_ms, "device_lookup_from_id");
        }
      else
        {
          device = wait_for_async(
            BluetoothLEDevice::FromBluetoothAddressAsync(scan.address,
                                                         scan.address_type),
            options.operation_timeout_ms, "device_lookup_from_address");
        }

      if (!device)
        {
          throw tool_error(5, "device lookup returned null after scan");
        }

      std::cout << "BLEGATTC AEP_RESOLVE source="
                << (use_association_id ? "FromIdAsync" :
                                         "FromBluetoothAddressAsync")
                << " attempt=" << attempt << "/"
                << options.connect_attempts << "\n";

      const DeviceAccessStatus access = wait_for_async(
        device.RequestAccessAsync(), options.operation_timeout_ms,
        "device_access");
      std::cout << "BLEGATTC ACCESS attempt=" << attempt
                << " status=" << device_access_status_name(access) << "\n";
      if (access != DeviceAccessStatus::Allowed)
        {
          throw tool_error(6, std::string("Bluetooth device access status=") +
                                device_access_status_name(access));
        }

      std::cout << "BLEGATTC DEVICE attempt=" << attempt << " address="
                << format_address(device.BluetoothAddress()) << " name=\""
                << json_escape(winrt::to_string(device.Name())) << "\""
                << " connection="
                << connection_status_name(device.ConnectionStatus()) << "\n";

      GattSession candidate_session = wait_for_async(
        GattSession::FromDeviceIdAsync(device.BluetoothDeviceId()),
        options.operation_timeout_ms, "gatt_session");
      if (!candidate_session)
        {
          throw tool_error(5, "GATT session lookup returned null");
        }

      gatt_session_lease candidate_lease(candidate_session);
      const bool candidate_maintaining = candidate_lease.maintaining();

      std::cout << "BLEGATTC SESSION_REQUEST attempt=" << attempt
                << " maintain_requested="
                << (candidate_maintaining ? 1 : 0)
                << " status="
                << session_status_name(candidate_session.SessionStatus())
                << " max_pdu=" << candidate_session.MaxPduSize() << "\n";

      /* MaintainConnection is an asynchronous connection request.  The
       * Qualcomm desktop stack can otherwise accept a service query while the
       * GATT session is still Closed, establish only a Controller link, and
       * leave the query pending without sending ATT.  The targeted recovery
       * path therefore proves a bounded Active session before issuing its
       * first UUID query.  Normal and historical cached paths keep their
       * already-verified ordering.
       */

      if (options.n13_targeted_discovery && candidate_maintaining)
        {
          const auto active_deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(options.operation_timeout_ms);
          while (candidate_session.SessionStatus() !=
                   GattSessionStatus::Active &&
                 std::chrono::steady_clock::now() < active_deadline)
            {
              std::this_thread::sleep_for(20ms);
            }

          if (candidate_session.SessionStatus() != GattSessionStatus::Active)
            {
              throw tool_error(8, "timeout stage=targeted_session_active");
            }

          std::cout << "BLEGATTC SESSION_PRIMED attempt=" << attempt
                    << " status=Active connection="
                    << connection_status_name(device.ConnectionStatus())
                    << "\n";
        }

      /* MaintainConnection asks drivers such as Qualcomm FastConnect to
       * initiate the link, but an Active status can still describe a stale
       * logical Windows session with no RF connection.  Normal mode therefore
       * requires uncached service discovery as the first ATT proof.  The
       * explicit N13 negative-recovery mode resolves only the two exact cached
       * service device instances.  This bypasses a known-stuck device-wide
       * enumeration, but it is not declared LINK_READY until the later
       * uncached Device Name read succeeds.
       */

      if (options.n13_cached_discovery)
        {
          services.push_back(open_cached_service_instance(
            device, GattServiceUuids::GenericAccess(), options, "gap"));
          services.push_back(open_cached_service_instance(
            device, kN13ServiceUuid, options, "n13"));
          connected_attempt = attempt;
          session = candidate_session;
          candidate_lease.detach();
          candidate_session = nullptr;
          std::cout << "BLEGATTC SERVICE_INDEX_READY attempt=" << attempt
                    << " services=" << services.size()
                    << " cache=cached source=targeted_cache"
                    << " connection="
                    << connection_status_name(device.ConnectionStatus())
                    << "\n";
          break;
        }

      if (options.n13_targeted_discovery)
        {
          services.push_back(open_targeted_uncached_service(
            device, GattServiceUuids::GenericAccess(), options, "gap"));
          services.push_back(open_targeted_uncached_service(
            device, kN13ServiceUuid, options, "n13"));
          connected_attempt = attempt;
          session = candidate_session;
          candidate_lease.detach();
          candidate_session = nullptr;
          std::cout << "BLEGATTC LINK_READY attempt=" << attempt
                    << " services=" << services.size()
                    << " cache=uncached source=targeted_uuid"
                    << " connection="
                    << connection_status_name(device.ConnectionStatus())
                    << "\n";
          break;
        }

      auto services_result = wait_for_async(
        device.GetGattServicesAsync(BluetoothCacheMode::Uncached),
        options.operation_timeout_ms, "gatt_service_discovery");
      const GattCommunicationStatus service_status = services_result.Status();
      if (service_status == GattCommunicationStatus::Success)
        {
          for (const auto &candidate : services_result.Services())
            {
              services.push_back(candidate);
            }

          connected_attempt = attempt;
          session = candidate_session;
          candidate_lease.detach();
          candidate_session = nullptr;
          std::cout << "BLEGATTC LINK_READY attempt=" << attempt
                    << " services=" << services.size()
                    << " cache=uncached"
                    << " connection="
                    << connection_status_name(device.ConnectionStatus())
                    << "\n";
          break;
        }

      candidate_lease.release();
      candidate_session = nullptr;
      device.Close();
      device = nullptr;
      if (service_status != GattCommunicationStatus::Unreachable ||
          attempt == options.connect_attempts)
        {
          throw tool_error(
            7, std::string("GATT service discovery failed status=") +
                 gatt_status_name(service_status));
        }

      std::cout << "BLEGATTC CONNECT_RETRY failed_attempt=" << attempt
                << " next_attempt=" << (attempt + 1)
                << " status=" << gatt_status_name(service_status) << "\n";
      std::this_thread::sleep_for(kConnectRetryDelay);
    }

  if (!device || !session || connected_attempt == 0)
    {
      throw tool_error(7, "GATT link did not become usable");
    }

  const bool maintain_capable = session.CanMaintainConnection();
  const GattSessionStatus initial_session_status = session.SessionStatus();
  gatt_session_lease session_lease(session);

  std::cout << "BLEGATTC SESSION attempt=" << connected_attempt
            << " maintain_capable=" << (maintain_capable ? 1 : 0)
            << " maintain_requested="
            << (session_lease.maintaining() ? 1 : 0)
            << " status=" << session_status_name(initial_session_status)
            << " max_pdu=" << session.MaxPduSize() << "\n";

  if (session_lease.maintaining())
    {
      const auto connection_deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(options.operation_timeout_ms);
      while (session.SessionStatus() != GattSessionStatus::Active &&
             std::chrono::steady_clock::now() < connection_deadline)
        {
          std::this_thread::sleep_for(20ms);
        }

      if (session.SessionStatus() != GattSessionStatus::Active)
        {
          throw tool_error(8, "timeout stage=gatt_session_active");
        }

      std::cout << "BLEGATTC SESSION_READY status="
                << session_status_name(session.SessionStatus())
                << " connection="
                << connection_status_name(device.ConnectionStatus())
                << " max_pdu=" << session.MaxPduSize() << "\n";
    }

  GattDeviceService service{nullptr};
  GattDeviceService n13_service{nullptr};
  unsigned int gap_service_count = 0;
  unsigned int n13_service_count = 0;
  for (const auto &candidate : services)
    {
      if (candidate.Uuid() == GattServiceUuids::GenericAccess())
        {
          service = candidate;
          ++gap_service_count;
        }
      else if (candidate.Uuid() == kN13ServiceUuid)
        {
          n13_service = candidate;
          ++n13_service_count;
        }
    }

  if (gap_service_count != 1)
    {
      throw tool_error(7, "expected exactly one GAP service");
    }

  request_service_access(service, options, "gap", "gap_service_access");
  auto characteristics_result = wait_for_async(
    service.GetCharacteristicsAsync(discovery_cache_mode(options)),
    options.operation_timeout_ms, "device_name_discovery");
  if (characteristics_result.Status() != GattCommunicationStatus::Success)
    {
      throw tool_error(
        7, std::string("Device Name discovery failed status=") +
             gatt_status_name(characteristics_result.Status()));
    }

  GattCharacteristic characteristic{nullptr};
  unsigned int device_name_count = 0;
  for (const auto &candidate : characteristics_result.Characteristics())
    {
      if (candidate.Uuid() == GattCharacteristicUuids::GapDeviceName())
        {
          characteristic = candidate;
          ++device_name_count;
        }
    }

  if (device_name_count != 1)
    {
      throw tool_error(7, "expected exactly one Device Name characteristic");
    }

  auto read_result = wait_for_async(
    characteristic.ReadValueAsync(BluetoothCacheMode::Uncached),
    options.operation_timeout_ms, "device_name_read");
  if (read_result.Status() != GattCommunicationStatus::Success)
    {
      throw tool_error(
        7, std::string("Device Name read failed status=") +
             gatt_status_name(read_result.Status()));
    }

  const std::vector<std::uint8_t> value = read_buffer(read_result.Value());
  const std::string device_name(value.begin(), value.end());
  std::cout << "BLEGATTC READ service=1800 characteristic=2a00"
            << " value_hex=" << bytes_to_hex(value) << " value=\""
            << json_escape(device_name) << "\"\n";

  if (options.n13_cached_discovery)
    {
      std::cout << "BLEGATTC LINK_READY proof=uncached_device_name_read"
                << " cache=cached connection="
                << connection_status_name(device.ConnectionStatus())
                << "\n";
    }

  if (!options.expected_device_name.empty() &&
      device_name != options.expected_device_name)
    {
      throw tool_error(7, "Device Name value did not match expectation");
    }

  n13_result_s n13;
  if (options.n13)
    {
      if (n13_service_count != 1)
        {
          throw tool_error(7, "expected exactly one N13 service");
        }

      n13 = run_n13_gate(n13_service, options);
    }

  std::cout << "BLEGATTC SESSION_CONNECTED status="
            << session_status_name(session.SessionStatus())
            << " connection="
            << connection_status_name(device.ConnectionStatus())
            << " max_pdu=" << session.MaxPduSize() << "\n";

  for (auto &candidate : services)
    {
      candidate.Close();
    }

  services.clear();
  session_lease.release();
  device.Close();
  characteristic = nullptr;
  service = nullptr;
  n13_service = nullptr;
  session = nullptr;
  device = nullptr;

  bool rediscovered = false;
  if (options.rediscover)
    {
      std::this_thread::sleep_for(500ms);
      const scan_result_s after = scan_target(
        options, options.rediscover_timeout_ms, "post_close_rediscovery");
      rediscovered = after.address == scan.address;
      std::cout << "BLEGATTC REDISCOVER address="
                << format_address(after.address) << " address_type="
                << address_type_name(after.address_type) << " name=\""
                << json_escape(after.local_name) << "\" rssi=" << after.rssi
                << "\n";
    }

  write_result_file(options, scan, device_name, rediscovered, n13);
  std::cout << "BLEGATTC RESULT PASS gap_read=1 n13="
            << (n13.executed ? 1 : 0) << " negative="
            << (n13.negative_executed ? 1 : 0) << " rediscovered="
            << (rediscovered ? 1 : 0) << " discovery_cache="
            << discovery_cache_name(options) << "\n";
  return 0;
}
} // namespace

int wmain(int argc, wchar_t **argv)
{
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  try
    {
      clear_result_file_arguments(argc, argv);
      const options_s options = parse_options(argc, argv);
      return run(options);
    }
  catch (const tool_error &error)
    {
      std::cerr << "BLEGATTC ERROR " << error.what() << "\n";
      return error.exit_code();
    }
  catch (const winrt::hresult_error &error)
    {
      std::wcerr << L"BLEGATTC ERROR HRESULT=0x" << std::hex
                 << static_cast<std::uint32_t>(error.code().value)
                 << L" message=" << error.message().c_str() << L"\n";
      return 6;
    }
  catch (const std::exception &error)
    {
      std::cerr << "BLEGATTC ERROR " << error.what() << "\n";
      return 2;
    }
}
