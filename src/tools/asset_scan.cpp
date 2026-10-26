#include "smg2/data/bcsv.hpp"
#include "smg2/data/rarc.hpp"
#include "smg2/data/u8.hpp"
#include "smg2/data/yaz0.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using smg2::data::BcsvTable;
using smg2::data::RarcArchive;
using smg2::data::U8Archive;
using smg2::data::Yaz0;

namespace {
std::vector<std::uint8_t> read_bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open input");
    const auto size = file.tellg();
    if (size < 0 || size > 512 * 1024 * 1024) throw std::runtime_error("Input size invalid or exceeds 512 MiB");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (size != 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("Input read incomplete");
    }
    return bytes;
}

// Game strings are byte strings (often Shift-JIS), not assumed to be UTF-8.
// Escaping high bytes as U+00NN keeps their exact byte values recoverable.
std::string json(std::string_view value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 0x20 || c >= 0x80) {
            out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(c) << std::dec;
        } else out << static_cast<char>(c);
    }
    return out.str() + '"';
}

void vector_fields(std::ostream& out, const BcsvTable& table, std::size_t row,
                   const std::array<const char*, 3>& names) {
    out << '[';
    bool comma = false;
    for (const char* name : names) {
        if (comma) out << ',';
        comma = true;
        const auto field = table.find_field(name);
        const auto value = field ? table.float_value(row, *field) : std::nullopt;
        if (value && std::isfinite(*value)) out << std::setprecision(9) << *value;
        else out << "null";
    }
    out << ']';
}

struct Counts { std::size_t archives{}, files{}, tables{}, rows{}, objects{}, errors{}; };

template<typename Archive>
std::string scan_contents(const Archive& archive, const fs::path& path,
                          const fs::path& root, Counts& totals, bool compressed,
                          std::string_view format) {
    Counts local;
    std::ostringstream out;
    out << "{\"path\":" << json(fs::relative(path, root).generic_string())
        << ",\"root\":" << json(archive.rootName())
        << ",\"archive_format\":" << json(format)
        << ",\"yaz0\":" << (compressed ? "true" : "false") << ",\"tables\":[";
    bool comma = false;
    for (const auto& entry : archive.entries()) {
        if (entry.kind != decltype(entry.kind)::file) continue;
        ++local.files;
        const auto extension = fs::path(entry.name).extension().string();
        // Stage JMap tables under jmp/ have no filename extension.
        if (extension != ".bcsv" && extension != ".bcam" && !entry.path.starts_with("jmp/")) continue;
        const auto bytes = archive.readFile(entry);
        const auto expanded = Yaz0::hasSignature(bytes) ? Yaz0::decompress(bytes) : std::vector<std::uint8_t>{};
        const auto table = BcsvTable::parse(expanded.empty() ? bytes : std::span<const std::uint8_t>(expanded));
        ++local.tables;
        local.rows += table.row_count();
        if (comma) out << ',';
        comma = true;
        out << "{\"path\":" << json(entry.path) << ",\"rows\":" << table.row_count()
            << ",\"fields\":" << table.fields().size() << ",\"objects\":[";
        const auto name = table.find_field("name");
        const bool positions = table.find_field("pos_x").has_value() &&
                               table.find_field("pos_y").has_value() &&
                               table.find_field("pos_z").has_value();
        bool object_comma = false;
        if (name && positions) {
            for (std::size_t row = 0; row < table.row_count(); ++row) {
                const auto value = table.string_value(row, *name);
                if (!value) continue;
                if (object_comma) out << ',';
                object_comma = true;
                ++local.objects;
                out << "{\"row\":" << row << ",\"name\":" << json(*value) << ",\"position\":";
                vector_fields(out, table, row, {"pos_x", "pos_y", "pos_z"});
                out << ",\"rotation\":";
                vector_fields(out, table, row, {"dir_x", "dir_y", "dir_z"});
                out << ",\"scale\":";
                vector_fields(out, table, row, {"scale_x", "scale_y", "scale_z"});
                out << '}';
            }
        }
        out << "]}";
    }
    out << "],\"file_count\":" << local.files << '}';
    totals.files += local.files;
    totals.tables += local.tables;
    totals.rows += local.rows;
    totals.objects += local.objects;
    return out.str();
}

std::string scan_archive(const fs::path& path, const fs::path& root, Counts& totals) {
    auto stored = read_bytes(path);
    const bool compressed = Yaz0::hasSignature(stored);
    const auto decoded = compressed ? Yaz0::decompress(stored) : std::move(stored);
    if (decoded.size() >= 4 && decoded[0] == 'R' && decoded[1] == 'A' &&
        decoded[2] == 'R' && decoded[3] == 'C') {
        return scan_contents(RarcArchive::parse(decoded), path, root, totals, compressed, "RARC");
    }
    if (decoded.size() >= 4 && decoded[0] == 0x55 && decoded[1] == 0xaa &&
        decoded[2] == 0x38 && decoded[3] == 0x2d) {
        return scan_contents(U8Archive::parse(decoded), path, root, totals, compressed, "U8");
    }
    throw std::runtime_error("Archive signature is neither RARC nor U8");
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: smg2_asset_scan <extracted DATA/files directory> <report.json>\n";
        return 2;
    }
    try {
        const fs::path root = fs::canonical(argv[1]);
        std::vector<fs::path> paths;
        for (const auto& item : fs::recursive_directory_iterator(root)) {
            if (item.is_regular_file() && item.path().extension() == ".arc") paths.push_back(item.path());
        }
        std::sort(paths.begin(), paths.end());
        std::ofstream out(argv[2]);
        if (!out) throw std::runtime_error("Cannot create output report");
        Counts totals;
        out << "{\"format\":\"SMG2 native asset scan v1\",\"string_encoding\":\"game raw bytes escaped as U+00NN\",\"archives\":[\n";
        bool comma = false;
        for (const auto& path : paths) {
            if (comma) out << ",\n";
            comma = true;
            ++totals.archives;
            try {
                out << scan_archive(path, root, totals);
            } catch (const std::exception& error) {
                ++totals.errors;
                out << "{\"path\":" << json(fs::relative(path, root).generic_string())
                    << ",\"error\":" << json(error.what()) << '}';
                std::cerr << fs::relative(path, root).string() << ": " << error.what() << '\n';
            }
        }
        out << "\n],\"summary\":{\"archives\":" << totals.archives << ",\"files\":" << totals.files
            << ",\"bcsv_tables\":" << totals.tables << ",\"bcsv_rows\":" << totals.rows
            << ",\"placed_objects\":" << totals.objects << ",\"errors\":" << totals.errors << "}}\n";
        out.close();
        if (!out) throw std::runtime_error("Output write failed");
        std::cout << "Archives: " << totals.archives << "; BCSV tables: " << totals.tables
                  << "; rows: " << totals.rows << "; placed objects: " << totals.objects
                  << "; errors: " << totals.errors << '\n';
        return totals.errors == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Asset scan failed: " << error.what() << '\n';
        return 1;
    }
}
