#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

enum class Endian {
  Little,
  Big
};

struct Reader {
  const std::vector<std::uint8_t>& data;
  Endian endian;

  void require(std::size_t offset, std::size_t size) const {
    if (offset > data.size() || size > data.size() - offset) {
      throw std::runtime_error("unexpected end of file");
    }
  }

  std::uint16_t u16(std::size_t offset) const {
    require(offset, 2);
    if (endian == Endian::Little) {
      return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
    }
    return static_cast<std::uint16_t>((data[offset] << 8) | data[offset + 1]);
  }

  std::uint32_t u32(std::size_t offset) const {
    require(offset, 4);
    if (endian == Endian::Little) {
      return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    }
    return (static_cast<std::uint32_t>(data[offset]) << 24)
      | (static_cast<std::uint32_t>(data[offset + 1]) << 16)
      | (static_cast<std::uint32_t>(data[offset + 2]) << 8)
      | static_cast<std::uint32_t>(data[offset + 3]);
  }

  std::uint64_t u64(std::size_t offset) const {
    const std::uint64_t a = u32(offset);
    const std::uint64_t b = u32(offset + 4);
    return endian == Endian::Little ? (a | (b << 32)) : ((a << 32) | b);
  }
};

struct PckHeader {
  Endian endian = Endian::Little;
  std::uint32_t headerSize = 0;
  std::uint32_t section1Size = 0;
  std::uint32_t section2Size = 0;
  std::uint32_t section3Size = 0;
  std::uint32_t section4Size = 0;
  std::size_t sectionOffset = 0;
};

struct FileEntry {
  std::uint64_t id = 0;
  std::uint32_t idHigh = 0;
  std::uint32_t idLow = 0;
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
  std::uint32_t languageId = 0;
  bool external64 = false;
};

std::string lower_extension(const fs::path& path) {
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return ext;
}

std::vector<std::uint8_t> read_all(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("cannot open input file: " + path.string());
  }
  return std::vector<std::uint8_t>(
    std::istreambuf_iterator<char>(file),
    std::istreambuf_iterator<char>()
  );
}

bool plausible_header(const Reader& reader, std::size_t fileSize) {
  const auto headerSize = reader.u32(0x04);
  const auto section1 = reader.u32(0x0c);
  const auto section2 = reader.u32(0x10);
  const auto section3 = reader.u32(0x14);
  if (headerSize < 0x10 || headerSize > fileSize) {
    return false;
  }
  const std::uint64_t minSize = static_cast<std::uint64_t>(section1) + section2 + section3 + 0x10;
  return minSize <= fileSize + 0x20;
}

PckHeader read_header(const std::vector<std::uint8_t>& data) {
  if (data.size() < 0x18 || std::string(reinterpret_cast<const char*>(data.data()), 4) != "AKPK") {
    throw std::runtime_error("not a Wwise AKPK package");
  }

  Reader little{ data, Endian::Little };
  const Endian endian = plausible_header(little, data.size()) ? Endian::Little : Endian::Big;
  Reader reader{ data, endian };

  PckHeader header;
  header.endian = endian;
  header.headerSize = reader.u32(0x04);
  header.section1Size = reader.u32(0x0c);
  header.section2Size = reader.u32(0x10);
  header.section3Size = reader.u32(0x14);
  header.sectionOffset = 0x18;

  const std::uint64_t sumWithoutExternals =
    static_cast<std::uint64_t>(header.section1Size) + header.section2Size + header.section3Size + 0x10;
  if (sumWithoutExternals < header.headerSize) {
    reader.require(0x18, 4);
    header.section4Size = reader.u32(0x18);
    header.sectionOffset = 0x1c;
  }

  return header;
}

std::string read_utf8_string(const std::vector<std::uint8_t>& data, std::size_t offset) {
  std::string out;
  while (offset < data.size() && data[offset] != 0) {
    out.push_back(static_cast<char>(data[offset++]));
  }
  return out;
}

std::string read_utf16_ascii_string(const Reader& reader, std::size_t offset) {
  std::string out;
  while (offset + 1 < reader.data.size()) {
    const auto ch = reader.u16(offset);
    if (ch == 0) {
      break;
    }
    out.push_back(ch < 0x80 ? static_cast<char>(ch) : '_');
    offset += 2;
  }
  return out;
}

std::unordered_map<std::uint32_t, std::string> parse_languages(
  const std::vector<std::uint8_t>& data,
  const Reader& reader,
  std::size_t offset,
  std::uint32_t sectionSize
) {
  std::unordered_map<std::uint32_t, std::string> languages;
  if (sectionSize == 0) {
    return languages;
  }

  reader.require(offset, sectionSize);
  const auto count = reader.u32(offset);
  std::size_t cursor = offset + 4;
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto languageOffset = reader.u32(cursor);
    const auto languageId = reader.u32(cursor + 4);
    const std::size_t stringOffset = offset + languageOffset;
    reader.require(stringOffset, 1);

    const bool utf16 = (data[stringOffset] == 0)
      || (stringOffset + 1 < data.size() && data[stringOffset + 1] == 0);
    auto name = utf16
      ? read_utf16_ascii_string(reader, stringOffset)
      : read_utf8_string(data, stringOffset);
    if (name.empty()) {
      name = "lang_" + std::to_string(languageId);
    }
    languages[languageId] = name;
    cursor += 8;
  }
  return languages;
}

std::uint32_t detect_bank_version(const Reader& reader, std::uint64_t offset) {
  if (offset + 12 > reader.data.size()) {
    return 62;
  }
  const auto version = reader.u32(static_cast<std::size_t>(offset + 8));
  return version > 0 && version <= 0x1000 ? version : 62;
}

std::vector<FileEntry> parse_table(
  const Reader& reader,
  std::size_t offset,
  std::uint32_t sectionSize,
  bool externals
) {
  std::vector<FileEntry> entries;
  if (sectionSize == 0) {
    return entries;
  }

  reader.require(offset, sectionSize);
  const auto count = reader.u32(offset);
  if (count == 0) {
    return entries;
  }

  const auto entrySize = (sectionSize - 4) / count;
  const bool altMode = entrySize == 0x18;
  std::size_t cursor = offset + 4;

  for (std::uint32_t i = 0; i < count; ++i) {
    FileEntry entry;
    if (altMode && externals) {
      if (reader.endian == Endian::Little) {
        entry.idLow = reader.u32(cursor);
        entry.idHigh = reader.u32(cursor + 4);
      } else {
        entry.idHigh = reader.u32(cursor);
        entry.idLow = reader.u32(cursor + 4);
      }
      entry.id = (static_cast<std::uint64_t>(entry.idHigh) << 32) | entry.idLow;
      entry.external64 = true;
      cursor += 8;
    } else {
      entry.id = reader.u32(cursor);
      cursor += 4;
    }

    const auto blockSize = reader.u32(cursor);
    cursor += 4;

    entry.size = altMode && !externals ? reader.u64(cursor) : reader.u32(cursor);
    cursor += altMode && !externals ? 8 : 4;

    entry.offset = reader.u32(cursor);
    cursor += 4;
    entry.languageId = reader.u32(cursor);
    cursor += 4;

    if (blockSize != 0) {
      entry.offset *= blockSize;
    }

    entries.push_back(entry);
  }

  return entries;
}

std::string entry_name(
  const FileEntry& entry,
  const std::unordered_map<std::uint32_t, std::string>& languages,
  const std::string& extension
) {
  fs::path path;
  if (entry.languageId != 0) {
    const auto found = languages.find(entry.languageId);
    path /= found == languages.end() ? "lang_" + std::to_string(entry.languageId) : found->second;
  }

  std::string filename;
  if (entry.external64) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%08x%08x.%s", entry.idHigh, entry.idLow, extension.c_str());
    filename = buffer;
  } else {
    filename = std::to_string(entry.id) + "." + extension;
  }
  path /= filename;
  return path.generic_string();
}

std::string legacy_sound_extension(const Reader& reader, const FileEntry& entry, std::uint32_t bankVersion) {
  if (bankVersion >= 62 || entry.offset + 0x16 > reader.data.size()) {
    return "wem";
  }
  const auto codec = reader.u16(static_cast<std::size_t>(entry.offset + 0x14));
  if (codec == 0x0401 || codec == 0x0166) {
    return "xma";
  }
  if (codec == 0xffff) {
    return "ogg";
  }
  return "wav";
}

void write_entry(const fs::path& root, const std::vector<std::uint8_t>& data, const FileEntry& entry, const std::string& name) {
  if (entry.offset > data.size() || entry.size > data.size() - entry.offset) {
    throw std::runtime_error("entry outside archive: " + name);
  }
  const auto output = root / fs::path(name);
  fs::create_directories(output.parent_path());
  std::ofstream file(output, std::ios::binary);
  if (!file) {
    throw std::runtime_error("cannot create output file: " + output.string());
  }
  file.write(reinterpret_cast<const char*>(data.data() + entry.offset), static_cast<std::streamsize>(entry.size));
}

fs::path archive_output_root(const fs::path& inputRoot, const fs::path& outputRoot, const fs::path& archive) {
  fs::path relative;
  try {
    relative = fs::relative(archive, inputRoot);
  } catch (const std::exception&) {
    relative = archive.filename();
  }
  relative.replace_extension();
  return outputRoot / relative;
}

void extract_pck(const fs::path& inputRoot, const fs::path& outputRoot, const fs::path& archive) {
  const auto data = read_all(archive);
  const auto header = read_header(data);
  const Reader reader{ data, header.endian };

  std::size_t cursor = header.sectionOffset;
  const auto languages = parse_languages(data, reader, cursor, header.section1Size);
  cursor += header.section1Size;

  const auto banks = parse_table(reader, cursor, header.section2Size, false);
  cursor += header.section2Size;

  std::uint32_t bankVersion = 0;
  if (!banks.empty()) {
    bankVersion = detect_bank_version(reader, banks.front().offset);
  }
  if (bankVersion == 0) {
    bankVersion = 62;
  }

  const auto sounds = parse_table(reader, cursor, header.section3Size, false);
  cursor += header.section3Size;
  const auto externals = parse_table(reader, cursor, header.section4Size, true);

  const auto root = archive_output_root(inputRoot, outputRoot, archive);
  for (const auto& entry : banks) {
    write_entry(root, data, entry, entry_name(entry, languages, "bnk"));
  }
  for (const auto& entry : sounds) {
    write_entry(root, data, entry, entry_name(entry, languages, legacy_sound_extension(reader, entry, bankVersion)));
  }
  for (const auto& entry : externals) {
    write_entry(root, data, entry, entry_name(entry, languages, "wem"));
  }
}

std::vector<fs::path> find_pcks(const fs::path& input) {
  std::vector<fs::path> files;
  if (fs::is_regular_file(input)) {
    if (lower_extension(input) == ".pck") {
      files.push_back(input);
    }
    return files;
  }

  for (const auto& entry : fs::recursive_directory_iterator(input)) {
    if (entry.is_regular_file() && lower_extension(entry.path()) == ".pck") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

unsigned parse_threads(int argc, char** argv) {
  unsigned threads = std::max(1u, std::thread::hardware_concurrency());
  for (int i = 3; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--threads" && i + 1 < argc) {
      threads = static_cast<unsigned>(std::max(1, std::stoi(argv[++i])));
    }
  }
  return threads;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "Usage: hoyo-pck-extract <input-pck-or-directory> <output-directory> [--threads N]\n";
    return 2;
  }

  try {
    const fs::path input = fs::absolute(argv[1]);
    const fs::path output = fs::absolute(argv[2]);
    const auto threads = parse_threads(argc, argv);
    const auto inputRoot = fs::is_regular_file(input) ? input.parent_path() : input;
    const auto files = find_pcks(input);

    std::cout << "Found " << files.size() << " pck files\n";
    fs::create_directories(output);

    std::atomic<std::size_t> next{ 0 };
    std::atomic<std::size_t> done{ 0 };
    std::mutex errorsMutex;
    std::vector<std::string> errors;
    std::vector<std::thread> workers;
    const auto progressStep = std::max<std::size_t>(1, files.size() / 100);

    const auto workerCount = std::min<std::size_t>(threads, std::max<std::size_t>(1, files.size()));
    for (std::size_t i = 0; i < workerCount; ++i) {
      workers.emplace_back([&]() {
        while (true) {
          const auto index = next.fetch_add(1);
          if (index >= files.size()) {
            break;
          }
          try {
            extract_pck(inputRoot, output, files[index]);
          } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock(errorsMutex);
            errors.push_back(files[index].string() + ": " + error.what());
          }
          const auto current = done.fetch_add(1) + 1;
          if (current == files.size() || current % progressStep == 0) {
            std::cout << "Processed " << current << "/" << files.size() << "\n";
          }
        }
      });
    }
    for (auto& worker : workers) {
      worker.join();
    }

    if (!errors.empty()) {
      for (const auto& error : errors) {
        std::cerr << error << "\n";
      }
      return 1;
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
}
