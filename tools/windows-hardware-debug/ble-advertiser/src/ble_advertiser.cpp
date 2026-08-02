// SPDX-License-Identifier: Apache-2.0

#include <windows.h>

#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace std::chrono_literals;
using winrt::Windows::Devices::Bluetooth::Advertisement::
  BluetoothLEAdvertisementPublisher;
using winrt::Windows::Devices::Bluetooth::Advertisement::
  BluetoothLEAdvertisementPublisherStatus;
using winrt::Windows::Devices::Bluetooth::Advertisement::
  BluetoothLEManufacturerData;
using winrt::Windows::Devices::Bluetooth::BluetoothAdapter;
using winrt::Windows::Storage::Streams::DataWriter;

constexpr std::size_t kPortablePayloadLimit = 20;
constexpr unsigned int kDefaultCompanyId = 0xfffe;

std::atomic_bool g_stop_requested{false};

struct options_s
{
  unsigned int company_id{kDefaultCompanyId};
  std::vector<std::uint8_t> payload;
  unsigned int duration_seconds{30};
  unsigned int startup_timeout_ms{5000};
  std::filesystem::path ready_file;
  bool probe_only{false};
};

BOOL WINAPI console_handler(DWORD event)
{
  if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT ||
      event == CTRL_CLOSE_EVENT)
    {
      g_stop_requested.store(true);
      return TRUE;
    }

  return FALSE;
}

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
      throw std::runtime_error("payload is not valid Unicode");
    }

  std::string result(static_cast<std::size_t>(size), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(), size,
                          nullptr, nullptr) != size)
    {
      throw std::runtime_error("failed to encode payload as UTF-8");
    }

  return result;
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

std::vector<std::uint8_t> parse_hex(std::wstring text)
{
  text.erase(std::remove_if(text.begin(), text.end(), [](wchar_t ch) {
               return ch == L':' || ch == L'-' || ch == L',' || ch == L' ';
             }),
             text.end());

  if (text.empty() || (text.size() % 2) != 0)
    {
      throw std::runtime_error("--payload-hex expects an even number of digits");
    }

  std::vector<std::uint8_t> bytes;
  bytes.reserve(text.size() / 2);
  for (std::size_t offset = 0; offset < text.size(); offset += 2)
    {
      std::size_t consumed = 0;
      unsigned long value = 0;
      try
        {
          value = std::stoul(text.substr(offset, 2), &consumed, 16);
        }
      catch (const std::exception &)
        {
          throw std::runtime_error("--payload-hex contains a non-hex digit");
        }

      if (consumed != 2 || value > 0xff)
        {
          throw std::runtime_error("--payload-hex contains a non-hex digit");
        }

      bytes.push_back(static_cast<std::uint8_t>(value));
    }

  return bytes;
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

const char *status_name(BluetoothLEAdvertisementPublisherStatus status)
{
  switch (status)
    {
      case BluetoothLEAdvertisementPublisherStatus::Created:
        return "Created";
      case BluetoothLEAdvertisementPublisherStatus::Waiting:
        return "Waiting";
      case BluetoothLEAdvertisementPublisherStatus::Started:
        return "Started";
      case BluetoothLEAdvertisementPublisherStatus::Stopping:
        return "Stopping";
      case BluetoothLEAdvertisementPublisherStatus::Stopped:
        return "Stopped";
      case BluetoothLEAdvertisementPublisherStatus::Aborted:
        return "Aborted";
      default:
        return "Unknown";
    }
}

void print_usage()
{
  std::cout
    << "Windows BLE advertiser (manufacturer-data beacon)\n\n"
    << "Usage:\n"
    << "  ble_advertiser.exe [options]\n\n"
    << "Options:\n"
    << "  --probe                     Check adapter peripheral-role support\n"
    << "  --company-id VALUE          Test company ID (default 0xFFFE)\n"
    << "  --payload TEXT              UTF-8 payload (default BK7258-N12V)\n"
    << "  --payload-hex HEX           Raw payload; excludes --payload\n"
    << "  --duration SECONDS          Broadcast time; 0 means until Ctrl+C\n"
    << "  --startup-timeout-ms MS     Wait for Started (default 5000)\n"
    << "  --ready-file PATH           Write JSON only after Started\n"
    << "  --help                      Show this help\n\n"
    << "The portable payload limit is 20 bytes. Windows owns flags and the\n"
    << "local-name fields, so this tool publishes manufacturer data only.\n";
}

options_s parse_options(int argc, wchar_t **argv)
{
  options_s options;
  const std::string default_payload = "BK7258-N12V";
  options.payload.assign(default_payload.begin(), default_payload.end());
  bool payload_seen = false;
  bool payload_hex_seen = false;

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
      std::wstring argument = argv[index];
      if (argument == L"--help" || argument == L"-h")
        {
          print_usage();
          std::exit(0);
        }
      else if (argument == L"--probe")
        {
          options.probe_only = true;
        }
      else if (argument == L"--company-id")
        {
          options.company_id =
            parse_unsigned(require_value(index, L"--company-id"), 0xffff,
                           "--company-id");
        }
      else if (argument == L"--payload")
        {
          std::string payload =
            utf8_from_wide(require_value(index, L"--payload"));
          options.payload.assign(payload.begin(), payload.end());
          payload_seen = true;
        }
      else if (argument == L"--payload-hex")
        {
          options.payload =
            parse_hex(require_value(index, L"--payload-hex"));
          payload_hex_seen = true;
        }
      else if (argument == L"--duration")
        {
          options.duration_seconds =
            parse_unsigned(require_value(index, L"--duration"), 86400,
                           "--duration");
        }
      else if (argument == L"--startup-timeout-ms")
        {
          options.startup_timeout_ms =
            parse_unsigned(require_value(index, L"--startup-timeout-ms"),
                           60000, "--startup-timeout-ms");
          if (options.startup_timeout_ms == 0)
            {
              throw std::runtime_error(
                "--startup-timeout-ms must be greater than zero");
            }
        }
      else if (argument == L"--ready-file")
        {
          options.ready_file = require_value(index, L"--ready-file");
        }
      else
        {
          throw std::runtime_error("unknown option: " +
                                   utf8_from_wide(argument));
        }
    }

  if (payload_seen && payload_hex_seen)
    {
      throw std::runtime_error(
        "--payload and --payload-hex are mutually exclusive");
    }

  if (!options.probe_only &&
      (options.payload.empty() ||
       options.payload.size() > kPortablePayloadLimit))
    {
      throw std::runtime_error("payload must contain 1 to 20 bytes");
    }

  return options;
}

void write_ready_file(const options_s &options, std::uint64_t address)
{
  if (options.ready_file.empty())
    {
      return;
    }

  std::error_code error;
  const auto parent = options.ready_file.parent_path();
  if (!parent.empty())
    {
      std::filesystem::create_directories(parent, error);
      if (error)
        {
          throw std::runtime_error("cannot create ready-file directory");
        }
    }

  std::ofstream output(options.ready_file, std::ios::binary | std::ios::trunc);
  if (!output)
    {
      throw std::runtime_error("cannot create ready file");
    }

  output << "{\n"
         << "  \"status\": \"Started\",\n"
         << "  \"company_id\": " << options.company_id << ",\n"
         << "  \"payload_hex\": \"" << bytes_to_hex(options.payload)
         << "\",\n"
         << "  \"adapter_address\": \"" << std::hex << std::setw(12)
         << std::setfill('0') << address << "\"\n"
         << "}\n";
  output.flush();
  if (!output)
    {
      throw std::runtime_error("cannot finalize ready file");
    }
}

void clear_ready_path(const std::filesystem::path &path)
{
  if (path.empty())
    {
      return;
    }

  std::error_code error;
  std::filesystem::remove(path, error);
  if (error)
    {
      throw std::runtime_error("cannot remove stale ready file");
    }
}

void clear_ready_file_arguments(int argc, wchar_t **argv)
{
  for (int index = 1; index + 1 < argc; ++index)
    {
      if (std::wstring(argv[index]) == L"--ready-file")
        {
          clear_ready_path(argv[++index]);
        }
    }
}

int run(const options_s &options)
{
  winrt::init_apartment(winrt::apartment_type::multi_threaded);

  if (!options.probe_only)
    {
      clear_ready_path(options.ready_file);
    }

  BluetoothAdapter adapter = BluetoothAdapter::GetDefaultAsync().get();
  if (!adapter)
    {
      std::cerr << "BLEADV ERROR no default Bluetooth adapter\n";
      return 3;
    }

  const bool low_energy = adapter.IsLowEnergySupported();
  const bool peripheral = adapter.IsPeripheralRoleSupported();
  const std::uint64_t address = adapter.BluetoothAddress();
  std::cout << "BLEADV ADAPTER address=" << std::hex << std::setw(12)
            << std::setfill('0') << address << std::dec
            << " low_energy=" << (low_energy ? 1 : 0)
            << " peripheral=" << (peripheral ? 1 : 0) << "\n";

  if (!low_energy || !peripheral)
    {
      std::cerr << "BLEADV ERROR adapter does not support the BLE peripheral "
                   "role\n";
      return 3;
    }

  if (options.probe_only)
    {
      std::cout << "BLEADV RESULT PASS probe_only=1\n";
      return 0;
    }

  BluetoothLEManufacturerData manufacturer_data;
  manufacturer_data.CompanyId(
    static_cast<std::uint16_t>(options.company_id));
  DataWriter writer;
  writer.WriteBytes(options.payload);
  manufacturer_data.Data(writer.DetachBuffer());

  BluetoothLEAdvertisementPublisher publisher;
  publisher.Advertisement().ManufacturerData().Append(manufacturer_data);
  publisher.IsAnonymous(false);
  publisher.UseExtendedAdvertisement(false);

  std::atomic<int> event_status{
    static_cast<int>(BluetoothLEAdvertisementPublisherStatus::Created)};
  std::atomic<int> event_error{0};
  auto status_revoker = publisher.StatusChanged(
    winrt::auto_revoke,
    [&](const BluetoothLEAdvertisementPublisher &,
        const auto &event_args) noexcept {
      event_status.store(static_cast<int>(event_args.Status()));
      event_error.store(static_cast<int>(event_args.Error()));
    });

  std::cout << "BLEADV CONFIG company_id=0x" << std::hex << std::setw(4)
            << std::setfill('0') << options.company_id << std::dec
            << " payload_hex=" << bytes_to_hex(options.payload)
            << " duration_sec=" << options.duration_seconds << "\n";

  publisher.Start();
  const auto startup_deadline = std::chrono::steady_clock::now() +
                                std::chrono::milliseconds(
                                  options.startup_timeout_ms);
  BluetoothLEAdvertisementPublisherStatus last_status = publisher.Status();
  std::cout << "BLEADV STATUS " << status_name(last_status) << "\n";

  while (last_status != BluetoothLEAdvertisementPublisherStatus::Started)
    {
      if (last_status == BluetoothLEAdvertisementPublisherStatus::Aborted)
        {
          std::cerr << "BLEADV ERROR publisher aborted error="
                    << event_error.load() << "\n";
          return 4;
        }

      if (std::chrono::steady_clock::now() >= startup_deadline)
        {
          publisher.Stop();
          std::cerr << "BLEADV ERROR startup timeout status="
                    << status_name(last_status)
                    << " event_status=" << event_status.load()
                    << " event_error=" << event_error.load() << "\n";
          return 4;
        }

      std::this_thread::sleep_for(50ms);
      auto current = publisher.Status();
      if (current != last_status)
        {
          last_status = current;
          std::cout << "BLEADV STATUS " << status_name(last_status) << "\n";
        }
    }

  try
    {
      write_ready_file(options, address);
    }
  catch (...)
    {
      publisher.Stop();
      throw;
    }
  std::cout << "BLEADV READY status=Started\n";
  std::cout.flush();

  const auto started_at = std::chrono::steady_clock::now();
  const auto deadline = started_at +
                        std::chrono::seconds(options.duration_seconds);
  bool aborted = false;
  while (!g_stop_requested.load() &&
         (options.duration_seconds == 0 ||
          std::chrono::steady_clock::now() < deadline))
    {
      const auto current = publisher.Status();
      if (current == BluetoothLEAdvertisementPublisherStatus::Aborted)
        {
          aborted = true;
          break;
        }

      std::this_thread::sleep_for(100ms);
    }

  publisher.Stop();
  const auto stop_deadline = std::chrono::steady_clock::now() + 5s;
  while (publisher.Status() !=
           BluetoothLEAdvertisementPublisherStatus::Stopped &&
         publisher.Status() !=
           BluetoothLEAdvertisementPublisherStatus::Aborted &&
         std::chrono::steady_clock::now() < stop_deadline)
    {
      std::this_thread::sleep_for(50ms);
    }

  const auto final_status = publisher.Status();
  std::cout << "BLEADV STATUS " << status_name(final_status) << "\n";
  if (aborted ||
      final_status == BluetoothLEAdvertisementPublisherStatus::Aborted)
    {
      std::cerr << "BLEADV ERROR publisher aborted error="
                << event_error.load() << "\n";
      return 4;
    }

  std::cout << "BLEADV RESULT PASS stopped=1\n";
  return 0;
}
} // namespace

int wmain(int argc, wchar_t **argv)
{
  try
    {
      clear_ready_file_arguments(argc, argv);
      const options_s options = parse_options(argc, argv);
      SetConsoleCtrlHandler(console_handler, TRUE);
      return run(options);
    }
  catch (const winrt::hresult_error &error)
    {
      std::wcerr << L"BLEADV ERROR HRESULT=0x" << std::hex
                 << static_cast<std::uint32_t>(error.code().value)
                 << L" message=" << error.message().c_str() << L"\n";
      return 6;
    }
  catch (const std::exception &error)
    {
      std::cerr << "BLEADV ERROR " << error.what() << "\n";
      return 2;
    }
}
