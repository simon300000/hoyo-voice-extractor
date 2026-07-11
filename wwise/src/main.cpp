#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "libvgmstream.h"
#include "libvgmstream_streamfile.h"
#include "wav_utils.h"
}

namespace fs = std::filesystem;

#pragma pack(push, 1)
struct Section {
  char sign[4];
  std::uint32_t size;
};

struct Index {
  std::uint32_t id;
  std::uint32_t offset;
  std::uint32_t size;
};
#pragma pack(pop)

struct BnkEntry {
  std::uint32_t id = 0;
  std::uint32_t offset = 0;
  std::uint32_t size = 0;
};

std::string lower_extension(const fs::path& path) {
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return ext;
}

template <typename T>
bool read_value(std::istream& file, T& value) {
  return static_cast<bool>(file.read(reinterpret_cast<char*>(&value), sizeof(value)));
}

bool section_is(const Section& section, const char* sign) {
  return std::strncmp(section.sign, sign, 4) == 0;
}

std::vector<BnkEntry> read_bnk_entries(const fs::path& bnk, std::uint64_t& dataOffset) {
  std::ifstream file(bnk, std::ios::binary);
  if (!file) {
    throw std::runtime_error("cannot open bnk: " + bnk.string());
  }

  std::vector<BnkEntry> entries;
  Section section{};
  while (read_value(file, section)) {
    const auto sectionPayload = static_cast<std::uint64_t>(file.tellg());
    if (section_is(section, "DIDX")) {
      if (section.size % sizeof(Index) != 0) {
        throw std::runtime_error("malformed DIDX section: " + bnk.string());
      }
      for (std::uint32_t offset = 0; offset < section.size; offset += sizeof(Index)) {
        Index index{};
        if (!read_value(file, index)) {
          throw std::runtime_error("truncated DIDX section: " + bnk.string());
        }
        if (index.size != 0) {
          entries.push_back(BnkEntry{ index.id, index.offset, index.size });
        }
      }
    } else if (section_is(section, "DATA")) {
      dataOffset = static_cast<std::uint64_t>(file.tellg());
    }

    file.seekg(static_cast<std::streamoff>(sectionPayload + section.size), std::ios::beg);
  }

  return entries;
}

fs::path relative_output(const fs::path& inputRoot, const fs::path& outputRoot, const fs::path& input, const char* extension) {
  fs::path relative;
  try {
    relative = fs::relative(input, inputRoot);
  } catch (const std::exception&) {
    relative = input.filename();
  }
  relative.replace_extension(extension);
  return outputRoot / relative;
}

void decode_wem_to_wav(const fs::path& input, const fs::path& output) {
  fs::create_directories(output.parent_path());

  libstreamfile_t* streamFile = libstreamfile_open_from_stdio(input.string().c_str());
  if (!streamFile) {
    throw std::runtime_error("vgmstream cannot open: " + input.string());
  }

  libvgmstream_config_t config{};
  config.ignore_loop = true;
  config.force_sfmt = LIBVGMSTREAM_SFMT_PCM16;

  libvgmstream_t* stream = libvgmstream_create(streamFile, 0, &config);
  libstreamfile_close(streamFile);
  if (!stream) {
    throw std::runtime_error("vgmstream cannot decode: " + input.string());
  }

  const auto cleanup = [&]() {
    libvgmstream_free(stream);
  };

  if (stream->format->play_samples < 0 || stream->format->play_samples > INT32_MAX) {
    cleanup();
    throw std::runtime_error("wav is too long for RIFF header: " + input.string());
  }

  std::ofstream wav(output, std::ios::binary);
  if (!wav) {
    cleanup();
    throw std::runtime_error("cannot create wav: " + output.string());
  }

  std::uint8_t header[0x100]{};
  wav_header_t wavHeader{};
  wavHeader.is_float = stream->format->sample_format == LIBVGMSTREAM_SFMT_FLOAT;
  wavHeader.sample_size = stream->format->sample_size;
  wavHeader.sample_count = static_cast<std::int32_t>(stream->format->play_samples);
  wavHeader.sample_rate = stream->format->sample_rate;
  wavHeader.channels = stream->format->channels;

  const auto headerSize = wav_make_header(header, sizeof(header), &wavHeader);
  if (headerSize == 0) {
    cleanup();
    throw std::runtime_error("cannot create wav header: " + output.string());
  }
  wav.write(reinterpret_cast<const char*>(header), static_cast<std::streamsize>(headerSize));

  while (!stream->decoder->done) {
    const auto result = libvgmstream_render(stream);
    if (result < 0) {
      cleanup();
      throw std::runtime_error("decode failed: " + input.string());
    }

    void* buffer = stream->decoder->buf;
    const auto bufferBytes = stream->decoder->buf_bytes;
    const auto bufferSamples = stream->decoder->buf_samples;
    wav_swap_samples_le(buffer, stream->format->channels * bufferSamples, stream->format->sample_size);
    wav.write(reinterpret_cast<const char*>(buffer), bufferBytes);
  }

  cleanup();
}

void convert_bnk(const fs::path& inputRoot, const fs::path& outputRoot, const fs::path& tempRoot, const fs::path& bnk) {
  std::uint64_t dataOffset = 0;
  const auto entries = read_bnk_entries(bnk, dataOffset);
  if (dataOffset == 0 || entries.empty()) {
    return;
  }

  std::ifstream file(bnk, std::ios::binary);
  if (!file) {
    throw std::runtime_error("cannot reopen bnk: " + bnk.string());
  }

  fs::path relative;
  try {
    relative = fs::relative(bnk, inputRoot);
  } catch (const std::exception&) {
    relative = bnk.filename();
  }
  relative.replace_extension();

  for (const auto& entry : entries) {
    const auto wem = tempRoot / relative / (std::to_string(entry.id) + ".wem");
    const auto wav = outputRoot / relative / (std::to_string(entry.id) + ".wav");
    fs::create_directories(wem.parent_path());

    std::vector<char> data(entry.size);
    file.seekg(static_cast<std::streamoff>(dataOffset + entry.offset), std::ios::beg);
    file.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (file.gcount() != static_cast<std::streamsize>(data.size())) {
      throw std::runtime_error("truncated DATA entry in: " + bnk.string());
    }

    {
      std::ofstream wemFile(wem, std::ios::binary);
      if (!wemFile) {
        throw std::runtime_error("cannot create temporary wem: " + wem.string());
      }
      wemFile.write(data.data(), static_cast<std::streamsize>(data.size()));
    }

    decode_wem_to_wav(wem, wav);
    std::error_code ignored;
    fs::remove(wem, ignored);
  }
}

std::vector<fs::path> find_inputs(const fs::path& input) {
  std::vector<fs::path> files;
  if (fs::is_regular_file(input)) {
    const auto ext = lower_extension(input);
    if (ext == ".wem" || ext == ".bnk") {
      files.push_back(input);
    }
    return files;
  }

  for (const auto& entry : fs::recursive_directory_iterator(input)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto ext = lower_extension(entry.path());
    if (ext == ".wem" || ext == ".bnk") {
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
    std::cerr << "Usage: hoyo-audio-convert <input-wem-bnk-or-directory> <output-directory> [--threads N]\n";
    return 2;
  }

  try {
    libvgmstream_set_log(LIBVGMSTREAM_LOG_LEVEL_NONE, nullptr);

    const fs::path input = fs::absolute(argv[1]);
    const fs::path output = fs::absolute(argv[2]);
    const auto threads = parse_threads(argc, argv);
    const auto inputRoot = fs::is_regular_file(input) ? input.parent_path() : input;
    const auto files = find_inputs(input);
    const auto tempRoot = output / ".hoyo-audio-convert-tmp";

    std::cout << "Found " << files.size() << " bnk/wem files\n" << std::flush;
    fs::create_directories(output);
    fs::create_directories(tempRoot);

    std::atomic<std::size_t> next{ 0 };
    std::atomic<std::size_t> done{ 0 };
    std::mutex errorsMutex;
    std::mutex progressMutex;
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
            const auto ext = lower_extension(files[index]);
            if (ext == ".wem") {
              decode_wem_to_wav(files[index], relative_output(inputRoot, output, files[index], ".wav"));
            } else if (ext == ".bnk") {
              convert_bnk(inputRoot, output, tempRoot, files[index]);
            }
          } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock(errorsMutex);
            errors.push_back(files[index].string() + ": " + error.what());
          }
          const auto current = done.fetch_add(1) + 1;
          if (current == files.size() || current % progressStep == 0) {
            std::lock_guard<std::mutex> lock(progressMutex);
            std::cout << "Processed " << current << "/" << files.size() << "\n" << std::flush;
          }
        }
      });
    }
    for (auto& worker : workers) {
      worker.join();
    }

    std::error_code ignored;
    fs::remove_all(tempRoot, ignored);

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
