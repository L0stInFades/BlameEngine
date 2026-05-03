#include "next/terminal/nvim_surface.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Next {
namespace {

enum class MsgpackType {
    Nil,
    Boolean,
    Integer,
    Float,
    String,
    Binary,
    Array,
    Map,
    Extension
};

struct MsgpackValue {
    MsgpackType type = MsgpackType::Nil;
    bool boolean = false;
    int64_t integer = 0;
    double floating = 0.0;
    int extensionType = 0;
    std::string text;
    std::vector<uint8_t> bytes;
    std::vector<MsgpackValue> array;
    std::map<std::string, MsgpackValue> map;
};

MsgpackValue Nil() {
    return {};
}

MsgpackValue Bool(bool value) {
    MsgpackValue out;
    out.type = MsgpackType::Boolean;
    out.boolean = value;
    return out;
}

MsgpackValue Int(int64_t value) {
    MsgpackValue out;
    out.type = MsgpackType::Integer;
    out.integer = value;
    return out;
}

MsgpackValue String(std::string value) {
    MsgpackValue out;
    out.type = MsgpackType::String;
    out.text = std::move(value);
    return out;
}

MsgpackValue Array(std::vector<MsgpackValue> value) {
    MsgpackValue out;
    out.type = MsgpackType::Array;
    out.array = std::move(value);
    return out;
}

MsgpackValue Map(std::map<std::string, MsgpackValue> value) {
    MsgpackValue out;
    out.type = MsgpackType::Map;
    out.map = std::move(value);
    return out;
}

bool IsNil(const MsgpackValue& value) {
    return value.type == MsgpackType::Nil;
}

bool IsArray(const MsgpackValue& value) {
    return value.type == MsgpackType::Array;
}

int64_t AsInt(const MsgpackValue& value, int64_t fallback = 0) {
    return value.type == MsgpackType::Integer ? value.integer : fallback;
}

std::string AsString(const MsgpackValue& value) {
    return value.type == MsgpackType::String ? value.text : std::string();
}

void AppendByte(std::vector<uint8_t>& out, uint8_t value) {
    out.push_back(value);
}

void AppendBigEndian(std::vector<uint8_t>& out, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) {
        const size_t shift = (bytes - 1 - i) * 8;
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xff));
    }
}

bool Need(size_t size, size_t offset, size_t count) {
    return offset + count <= size;
}

uint64_t ReadUnsigned(const std::vector<uint8_t>& data, size_t offset, size_t count) {
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) {
        value = (value << 8) | data[offset + i];
    }
    return value;
}

int64_t SignExtend(uint64_t value, size_t bytes) {
    if (bytes >= 8) {
        return static_cast<int64_t>(value);
    }
    const uint64_t signBit = uint64_t{1} << (bytes * 8 - 1);
    if ((value & signBit) == 0) {
        return static_cast<int64_t>(value);
    }
    const uint64_t mask = ~uint64_t{0} << (bytes * 8);
    return static_cast<int64_t>(value | mask);
}

std::string ToDebugString(const MsgpackValue& value);

class MsgpackCodec {
public:
    std::vector<uint8_t> Pack(const MsgpackValue& value) const {
        std::vector<uint8_t> out;
        PackInto(value, out);
        return out;
    }

    bool UnpackStream(std::vector<uint8_t>& buffer,
                      std::vector<MsgpackValue>& values,
                      std::string& error) const {
        size_t offset = 0;
        while (offset < buffer.size()) {
            const size_t start = offset;
            MsgpackValue value;
            bool needMore = false;
            if (!UnpackOne(buffer, offset, value, needMore, error)) {
                if (needMore) {
                    offset = start;
                    break;
                }
                return false;
            }
            values.push_back(std::move(value));
        }

        if (offset > 0) {
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(offset));
        }
        return true;
    }

private:
    void PackInto(const MsgpackValue& value, std::vector<uint8_t>& out) const {
        switch (value.type) {
        case MsgpackType::Nil:
            AppendByte(out, 0xc0);
            break;
        case MsgpackType::Boolean:
            AppendByte(out, value.boolean ? 0xc3 : 0xc2);
            break;
        case MsgpackType::Integer:
            PackInteger(value.integer, out);
            break;
        case MsgpackType::Float:
            AppendByte(out, 0xcb);
            static_assert(sizeof(double) == 8, "double must be 64-bit");
            {
                uint64_t bits = 0;
                std::memcpy(&bits, &value.floating, sizeof(bits));
                AppendBigEndian(out, bits, 8);
            }
            break;
        case MsgpackType::String:
            PackString(value.text, out);
            break;
        case MsgpackType::Binary:
            PackBinary(value.bytes, out);
            break;
        case MsgpackType::Array:
            PackArray(value.array, out);
            break;
        case MsgpackType::Map:
            PackMap(value.map, out);
            break;
        case MsgpackType::Extension:
            PackExtension(value, out);
            break;
        }
    }

    void PackInteger(int64_t value, std::vector<uint8_t>& out) const {
        if (value >= 0 && value <= 0x7f) {
            AppendByte(out, static_cast<uint8_t>(value));
        } else if (value >= -32 && value < 0) {
            AppendByte(out, static_cast<uint8_t>(0x100 + value));
        } else if (value >= 0 && value <= 0xff) {
            AppendByte(out, 0xcc);
            AppendBigEndian(out, static_cast<uint64_t>(value), 1);
        } else if (value >= 0 && value <= 0xffff) {
            AppendByte(out, 0xcd);
            AppendBigEndian(out, static_cast<uint64_t>(value), 2);
        } else if (value >= 0 && value <= 0xffffffffLL) {
            AppendByte(out, 0xce);
            AppendBigEndian(out, static_cast<uint64_t>(value), 4);
        } else if (value >= 0) {
            AppendByte(out, 0xcf);
            AppendBigEndian(out, static_cast<uint64_t>(value), 8);
        } else if (value >= -0x80) {
            AppendByte(out, 0xd0);
            AppendBigEndian(out, static_cast<uint64_t>(static_cast<int8_t>(value)), 1);
        } else if (value >= -0x8000) {
            AppendByte(out, 0xd1);
            AppendBigEndian(out, static_cast<uint64_t>(static_cast<int16_t>(value)), 2);
        } else if (value >= -0x80000000LL) {
            AppendByte(out, 0xd2);
            AppendBigEndian(out, static_cast<uint64_t>(static_cast<int32_t>(value)), 4);
        } else {
            AppendByte(out, 0xd3);
            AppendBigEndian(out, static_cast<uint64_t>(value), 8);
        }
    }

    void PackLength(std::vector<uint8_t>& out,
                    uint8_t fixBase,
                    uint8_t code8,
                    uint8_t code16,
                    uint8_t code32,
                    size_t length) const {
        if (fixBase != 0 && length <= 31) {
            AppendByte(out, static_cast<uint8_t>(fixBase | length));
        } else if (length <= 0xff) {
            AppendByte(out, code8);
            AppendBigEndian(out, length, 1);
        } else if (length <= 0xffff) {
            AppendByte(out, code16);
            AppendBigEndian(out, length, 2);
        } else {
            AppendByte(out, code32);
            AppendBigEndian(out, length, 4);
        }
    }

    void PackString(const std::string& value, std::vector<uint8_t>& out) const {
        PackLength(out, 0xa0, 0xd9, 0xda, 0xdb, value.size());
        out.insert(out.end(), value.begin(), value.end());
    }

    void PackBinary(const std::vector<uint8_t>& value, std::vector<uint8_t>& out) const {
        PackLength(out, 0, 0xc4, 0xc5, 0xc6, value.size());
        out.insert(out.end(), value.begin(), value.end());
    }

    void PackArray(const std::vector<MsgpackValue>& value, std::vector<uint8_t>& out) const {
        if (value.size() <= 15) {
            AppendByte(out, static_cast<uint8_t>(0x90 | value.size()));
        } else if (value.size() <= 0xffff) {
            AppendByte(out, 0xdc);
            AppendBigEndian(out, value.size(), 2);
        } else {
            AppendByte(out, 0xdd);
            AppendBigEndian(out, value.size(), 4);
        }
        for (const auto& item : value) {
            PackInto(item, out);
        }
    }

    void PackMap(const std::map<std::string, MsgpackValue>& value, std::vector<uint8_t>& out) const {
        if (value.size() <= 15) {
            AppendByte(out, static_cast<uint8_t>(0x80 | value.size()));
        } else if (value.size() <= 0xffff) {
            AppendByte(out, 0xde);
            AppendBigEndian(out, value.size(), 2);
        } else {
            AppendByte(out, 0xdf);
            AppendBigEndian(out, value.size(), 4);
        }
        for (const auto& item : value) {
            PackString(item.first, out);
            PackInto(item.second, out);
        }
    }

    void PackExtension(const MsgpackValue& value, std::vector<uint8_t>& out) const {
        const auto length = value.bytes.size();
        if (length == 1) {
            AppendByte(out, 0xd4);
        } else if (length == 2) {
            AppendByte(out, 0xd5);
        } else if (length == 4) {
            AppendByte(out, 0xd6);
        } else if (length == 8) {
            AppendByte(out, 0xd7);
        } else if (length == 16) {
            AppendByte(out, 0xd8);
        } else if (length <= 0xff) {
            AppendByte(out, 0xc7);
            AppendBigEndian(out, length, 1);
        } else if (length <= 0xffff) {
            AppendByte(out, 0xc8);
            AppendBigEndian(out, length, 2);
        } else {
            AppendByte(out, 0xc9);
            AppendBigEndian(out, length, 4);
        }
        AppendByte(out, static_cast<uint8_t>(value.extensionType));
        out.insert(out.end(), value.bytes.begin(), value.bytes.end());
    }

    bool UnpackOne(const std::vector<uint8_t>& data,
                   size_t& offset,
                   MsgpackValue& out,
                   bool& needMore,
                   std::string& error) const {
        if (!Need(data.size(), offset, 1)) {
            needMore = true;
            return false;
        }

        const uint8_t marker = data[offset++];

        if (marker <= 0x7f) {
            out = Int(marker);
            return true;
        }
        if (marker >= 0xe0) {
            out = Int(static_cast<int8_t>(marker));
            return true;
        }
        if ((marker & 0xe0) == 0xa0) {
            return ReadText(data, offset, marker & 0x1f, out, needMore);
        }
        if ((marker & 0xf0) == 0x90) {
            return ReadArray(data, offset, marker & 0x0f, out, needMore, error);
        }
        if ((marker & 0xf0) == 0x80) {
            return ReadMap(data, offset, marker & 0x0f, out, needMore, error);
        }

        switch (marker) {
        case 0xc0:
            out = Nil();
            return true;
        case 0xc2:
            out = Bool(false);
            return true;
        case 0xc3:
            out = Bool(true);
            return true;
        case 0xc4:
            return ReadLengthPrefixedBinary(data, offset, 1, out, needMore);
        case 0xc5:
            return ReadLengthPrefixedBinary(data, offset, 2, out, needMore);
        case 0xc6:
            return ReadLengthPrefixedBinary(data, offset, 4, out, needMore);
        case 0xc7:
            return ReadLengthPrefixedExtension(data, offset, 1, out, needMore);
        case 0xc8:
            return ReadLengthPrefixedExtension(data, offset, 2, out, needMore);
        case 0xc9:
            return ReadLengthPrefixedExtension(data, offset, 4, out, needMore);
        case 0xca:
            return ReadFloat(data, offset, 4, out, needMore);
        case 0xcb:
            return ReadFloat(data, offset, 8, out, needMore);
        case 0xcc:
            return ReadUnsignedInt(data, offset, 1, out, needMore);
        case 0xcd:
            return ReadUnsignedInt(data, offset, 2, out, needMore);
        case 0xce:
            return ReadUnsignedInt(data, offset, 4, out, needMore);
        case 0xcf:
            return ReadUnsignedInt(data, offset, 8, out, needMore);
        case 0xd0:
            return ReadSignedInt(data, offset, 1, out, needMore);
        case 0xd1:
            return ReadSignedInt(data, offset, 2, out, needMore);
        case 0xd2:
            return ReadSignedInt(data, offset, 4, out, needMore);
        case 0xd3:
            return ReadSignedInt(data, offset, 8, out, needMore);
        case 0xd4:
            return ReadExtension(data, offset, 1, out, needMore);
        case 0xd5:
            return ReadExtension(data, offset, 2, out, needMore);
        case 0xd6:
            return ReadExtension(data, offset, 4, out, needMore);
        case 0xd7:
            return ReadExtension(data, offset, 8, out, needMore);
        case 0xd8:
            return ReadExtension(data, offset, 16, out, needMore);
        case 0xd9:
            return ReadLengthPrefixedText(data, offset, 1, out, needMore);
        case 0xda:
            return ReadLengthPrefixedText(data, offset, 2, out, needMore);
        case 0xdb:
            return ReadLengthPrefixedText(data, offset, 4, out, needMore);
        case 0xdc:
            return ReadLengthPrefixedArray(data, offset, 2, out, needMore, error);
        case 0xdd:
            return ReadLengthPrefixedArray(data, offset, 4, out, needMore, error);
        case 0xde:
            return ReadLengthPrefixedMap(data, offset, 2, out, needMore, error);
        case 0xdf:
            return ReadLengthPrefixedMap(data, offset, 4, out, needMore, error);
        default:
            error = "unsupported msgpack marker 0x" + Hex(marker);
            return false;
        }
    }

    static std::string Hex(uint8_t value) {
        const char* digits = "0123456789abcdef";
        std::string out;
        out.push_back(digits[(value >> 4) & 0x0f]);
        out.push_back(digits[value & 0x0f]);
        return out;
    }

    bool ReadUnsignedInt(const std::vector<uint8_t>& data,
                         size_t& offset,
                         size_t width,
                         MsgpackValue& out,
                         bool& needMore) const {
        if (!Need(data.size(), offset, width)) {
            needMore = true;
            return false;
        }
        out = Int(static_cast<int64_t>(ReadUnsigned(data, offset, width)));
        offset += width;
        return true;
    }

    bool ReadSignedInt(const std::vector<uint8_t>& data,
                       size_t& offset,
                       size_t width,
                       MsgpackValue& out,
                       bool& needMore) const {
        if (!Need(data.size(), offset, width)) {
            needMore = true;
            return false;
        }
        out = Int(SignExtend(ReadUnsigned(data, offset, width), width));
        offset += width;
        return true;
    }

    bool ReadFloat(const std::vector<uint8_t>& data,
                   size_t& offset,
                   size_t width,
                   MsgpackValue& out,
                   bool& needMore) const {
        if (!Need(data.size(), offset, width)) {
            needMore = true;
            return false;
        }
        out.type = MsgpackType::Float;
        if (width == 4) {
            uint32_t bits = static_cast<uint32_t>(ReadUnsigned(data, offset, width));
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            out.floating = value;
        } else {
            uint64_t bits = ReadUnsigned(data, offset, width);
            double value = 0.0;
            std::memcpy(&value, &bits, sizeof(value));
            out.floating = value;
        }
        offset += width;
        return true;
    }

    bool ReadText(const std::vector<uint8_t>& data,
                  size_t& offset,
                  size_t length,
                  MsgpackValue& out,
                  bool& needMore) const {
        if (!Need(data.size(), offset, length)) {
            needMore = true;
            return false;
        }
        out = String(std::string(reinterpret_cast<const char*>(data.data() + offset), length));
        offset += length;
        return true;
    }

    bool ReadLengthPrefixedText(const std::vector<uint8_t>& data,
                                size_t& offset,
                                size_t width,
                                MsgpackValue& out,
                                bool& needMore) const {
        if (!Need(data.size(), offset, width)) {
            needMore = true;
            return false;
        }
        const size_t length = static_cast<size_t>(ReadUnsigned(data, offset, width));
        offset += width;
        return ReadText(data, offset, length, out, needMore);
    }

    bool ReadBinary(const std::vector<uint8_t>& data,
                    size_t& offset,
                    size_t length,
                    MsgpackValue& out,
                    bool& needMore) const {
        if (!Need(data.size(), offset, length)) {
            needMore = true;
            return false;
        }
        out.type = MsgpackType::Binary;
        out.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
                         data.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
        return true;
    }

    bool ReadLengthPrefixedBinary(const std::vector<uint8_t>& data,
                                  size_t& offset,
                                  size_t width,
                                  MsgpackValue& out,
                                  bool& needMore) const {
        if (!Need(data.size(), offset, width)) {
            needMore = true;
            return false;
        }
        const size_t length = static_cast<size_t>(ReadUnsigned(data, offset, width));
        offset += width;
        return ReadBinary(data, offset, length, out, needMore);
    }

    bool ReadExtension(const std::vector<uint8_t>& data,
                       size_t& offset,
                       size_t length,
                       MsgpackValue& out,
                       bool& needMore) const {
        if (!Need(data.size(), offset, length + 1)) {
            needMore = true;
            return false;
        }
        out.type = MsgpackType::Extension;
        out.extensionType = static_cast<int8_t>(data[offset++]);
        out.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
                         data.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
        return true;
    }

    bool ReadLengthPrefixedExtension(const std::vector<uint8_t>& data,
                                     size_t& offset,
                                     size_t width,
                                     MsgpackValue& out,
                                     bool& needMore) const {
        if (!Need(data.size(), offset, width)) {
            needMore = true;
            return false;
        }
        const size_t length = static_cast<size_t>(ReadUnsigned(data, offset, width));
        offset += width;
        return ReadExtension(data, offset, length, out, needMore);
    }

    bool ReadArray(const std::vector<uint8_t>& data,
                   size_t& offset,
                   size_t length,
                   MsgpackValue& out,
                   bool& needMore,
                   std::string& error) const {
        out.type = MsgpackType::Array;
        out.array.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            MsgpackValue item;
            if (!UnpackOne(data, offset, item, needMore, error)) {
                return false;
            }
            out.array.push_back(std::move(item));
        }
        return true;
    }

    bool ReadLengthPrefixedArray(const std::vector<uint8_t>& data,
                                 size_t& offset,
                                 size_t width,
                                 MsgpackValue& out,
                                 bool& needMore,
                                 std::string& error) const {
        if (!Need(data.size(), offset, width)) {
            needMore = true;
            return false;
        }
        const size_t length = static_cast<size_t>(ReadUnsigned(data, offset, width));
        offset += width;
        return ReadArray(data, offset, length, out, needMore, error);
    }

    bool ReadMap(const std::vector<uint8_t>& data,
                 size_t& offset,
                 size_t length,
                 MsgpackValue& out,
                 bool& needMore,
                 std::string& error) const {
        out.type = MsgpackType::Map;
        for (size_t i = 0; i < length; ++i) {
            MsgpackValue key;
            MsgpackValue value;
            if (!UnpackOne(data, offset, key, needMore, error)) {
                return false;
            }
            if (!UnpackOne(data, offset, value, needMore, error)) {
                return false;
            }
            out.map[ToDebugString(key)] = std::move(value);
        }
        return true;
    }

    bool ReadLengthPrefixedMap(const std::vector<uint8_t>& data,
                               size_t& offset,
                               size_t width,
                               MsgpackValue& out,
                               bool& needMore,
                               std::string& error) const {
        if (!Need(data.size(), offset, width)) {
            needMore = true;
            return false;
        }
        const size_t length = static_cast<size_t>(ReadUnsigned(data, offset, width));
        offset += width;
        return ReadMap(data, offset, length, out, needMore, error);
    }
};

std::string ToDebugString(const MsgpackValue& value) {
    switch (value.type) {
    case MsgpackType::Nil:
        return "nil";
    case MsgpackType::Boolean:
        return value.boolean ? "true" : "false";
    case MsgpackType::Integer:
        return std::to_string(value.integer);
    case MsgpackType::Float:
        return std::to_string(value.floating);
    case MsgpackType::String:
        return value.text;
    case MsgpackType::Binary:
        return "<binary:" + std::to_string(value.bytes.size()) + ">";
    case MsgpackType::Extension:
        return "<ext:" + std::to_string(value.extensionType) + "," + std::to_string(value.bytes.size()) + ">";
    case MsgpackType::Array: {
        std::string out = "[";
        for (size_t i = 0; i < value.array.size(); ++i) {
            if (i != 0) {
                out += ", ";
            }
            out += ToDebugString(value.array[i]);
        }
        out += "]";
        return out;
    }
    case MsgpackType::Map:
        return "<map:" + std::to_string(value.map.size()) + ">";
    }
    return "<unknown>";
}

class NvimGrid {
public:
    void Resize(uint32_t width, uint32_t height) {
        width_ = width;
        height_ = height;
        rows_.assign(height_, std::vector<NvimCell>(width_));
    }

    void Clear() {
        rows_.assign(height_, std::vector<NvimCell>(width_));
    }

    void ApplyLine(int64_t row, int64_t column, const MsgpackValue& cells) {
        if (row < 0 || column < 0 || static_cast<uint32_t>(row) >= height_ || !IsArray(cells)) {
            return;
        }

        uint32_t x = static_cast<uint32_t>(column);
        for (const auto& cellValue : cells.array) {
            if (!IsArray(cellValue) || cellValue.array.empty()) {
                continue;
            }

            const std::string text = AsString(cellValue.array[0]);
            const int64_t highlight = cellValue.array.size() >= 2 ? AsInt(cellValue.array[1]) : 0;
            const int64_t repeat = std::max<int64_t>(1, cellValue.array.size() >= 3 ? AsInt(cellValue.array[2], 1) : 1);

            for (int64_t i = 0; i < repeat; ++i) {
                if (x < width_) {
                    rows_[static_cast<size_t>(row)][x].text = text.empty() ? " " : text;
                    rows_[static_cast<size_t>(row)][x].highlightId = highlight;
                }
                ++x;
            }
        }
    }

    void Scroll(int64_t top, int64_t bottom, int64_t left, int64_t right, int64_t rows, int64_t columns) {
        if (columns != 0 || left != 0 || right != static_cast<int64_t>(width_)) {
            Clear();
            return;
        }

        const auto clampedTop = static_cast<size_t>(std::max<int64_t>(0, top));
        const auto clampedBottom = static_cast<size_t>(std::min<int64_t>(height_, bottom));
        if (clampedTop >= clampedBottom) {
            return;
        }

        if (rows > 0) {
            for (size_t y = clampedTop; y < clampedBottom; ++y) {
                const size_t source = y + static_cast<size_t>(rows);
                rows_[y] = source < clampedBottom ? rows_[source] : std::vector<NvimCell>(width_);
            }
        } else if (rows < 0) {
            for (size_t y = clampedBottom; y > clampedTop; --y) {
                const size_t target = y - 1;
                const int64_t source = static_cast<int64_t>(target) + rows;
                rows_[target] = source >= static_cast<int64_t>(clampedTop)
                    ? rows_[static_cast<size_t>(source)]
                    : std::vector<NvimCell>(width_);
            }
        }
    }

    void SetCursor(uint32_t row, uint32_t column) {
        cursor_.row = row;
        cursor_.column = column;
    }

    NvimSurfaceSnapshot Snapshot() const {
        NvimSurfaceSnapshot snapshot;
        snapshot.width = width_;
        snapshot.height = height_;
        snapshot.cursor = cursor_;
        snapshot.rows.reserve(rows_.size());

        for (const auto& row : rows_) {
            std::string line;
            for (const auto& cell : row) {
                line += cell.text.empty() ? " " : cell.text;
            }
            while (!line.empty() && line.back() == ' ') {
                line.pop_back();
            }
            snapshot.rows.push_back(std::move(line));
        }

        return snapshot;
    }

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    NvimCursor cursor_;
    std::vector<std::vector<NvimCell>> rows_;
};

class ChildProcess {
public:
    ~ChildProcess() {
        Terminate();
    }

    bool Start(const std::vector<std::string>& args, std::string& error) {
#ifdef _WIN32
        SECURITY_ATTRIBUTES securityAttributes;
        securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
        securityAttributes.lpSecurityDescriptor = nullptr;
        securityAttributes.bInheritHandle = TRUE;

        HANDLE childStdInRead = nullptr;
        HANDLE childStdOutWrite = nullptr;
        HANDLE childStdErrWrite = nullptr;

        if (!CreatePipe(&childStdInRead, &stdinWrite_, &securityAttributes, 0)) {
            error = "CreatePipe(stdin) failed: " + LastWin32Error();
            return false;
        }
        if (!SetHandleInformation(stdinWrite_, HANDLE_FLAG_INHERIT, 0)) {
            error = "SetHandleInformation(stdin) failed: " + LastWin32Error();
            CloseHandleIfValid(childStdInRead);
            CloseHandleIfValid(stdinWrite_);
            return false;
        }

        if (!CreatePipe(&stdoutRead_, &childStdOutWrite, &securityAttributes, 0)) {
            error = "CreatePipe(stdout) failed: " + LastWin32Error();
            CloseHandleIfValid(childStdInRead);
            CloseHandleIfValid(stdinWrite_);
            return false;
        }
        if (!SetHandleInformation(stdoutRead_, HANDLE_FLAG_INHERIT, 0)) {
            error = "SetHandleInformation(stdout) failed: " + LastWin32Error();
            CloseHandleIfValid(childStdInRead);
            CloseHandleIfValid(stdinWrite_);
            CloseHandleIfValid(stdoutRead_);
            CloseHandleIfValid(childStdOutWrite);
            return false;
        }

        childStdErrWrite = CreateFileA(
            "NUL",
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &securityAttributes,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (childStdErrWrite == INVALID_HANDLE_VALUE) {
            childStdErrWrite = nullptr;
        }

        STARTUPINFOA startupInfo;
        ZeroMemory(&startupInfo, sizeof(startupInfo));
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdInput = childStdInRead;
        startupInfo.hStdOutput = childStdOutWrite;
        startupInfo.hStdError = childStdErrWrite ? childStdErrWrite : GetStdHandle(STD_ERROR_HANDLE);

        ZeroMemory(&processInfo_, sizeof(processInfo_));

        std::string commandLine = BuildWindowsCommandLine(args);
        std::vector<char> commandLineBuffer(commandLine.begin(), commandLine.end());
        commandLineBuffer.push_back('\0');

        const BOOL ok = CreateProcessA(
            nullptr,
            commandLineBuffer.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo_);

        CloseHandleIfValid(childStdInRead);
        CloseHandleIfValid(childStdOutWrite);
        CloseHandleIfValid(childStdErrWrite);

        if (!ok) {
            error = "CreateProcess failed: " + LastWin32Error();
            CloseHandleIfValid(stdinWrite_);
            CloseHandleIfValid(stdoutRead_);
            return false;
        }

        return true;
#else
        int stdinPipe[2] = {-1, -1};
        int stdoutPipe[2] = {-1, -1};
        if (pipe(stdinPipe) != 0 || pipe(stdoutPipe) != 0) {
            error = "pipe failed: " + std::string(std::strerror(errno));
            ClosePipe(stdinPipe);
            ClosePipe(stdoutPipe);
            return false;
        }

        const pid_t child = fork();
        if (child < 0) {
            error = "fork failed: " + std::string(std::strerror(errno));
            ClosePipe(stdinPipe);
            ClosePipe(stdoutPipe);
            return false;
        }

        if (child == 0) {
            dup2(stdinPipe[0], STDIN_FILENO);
            dup2(stdoutPipe[1], STDOUT_FILENO);

            const int devNull = open("/dev/null", O_WRONLY);
            if (devNull >= 0) {
                dup2(devNull, STDERR_FILENO);
                close(devNull);
            }

            ClosePipe(stdinPipe);
            ClosePipe(stdoutPipe);

            std::vector<char*> argv;
            argv.reserve(args.size() + 1);
            for (const auto& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);
            execvp(argv[0], argv.data());
            _exit(127);
        }

        pid_ = child;
        stdinFd_ = stdinPipe[1];
        stdoutFd_ = stdoutPipe[0];
        close(stdinPipe[0]);
        close(stdoutPipe[1]);

        const int flags = fcntl(stdoutFd_, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(stdoutFd_, F_SETFL, flags | O_NONBLOCK);
        }

        return true;
#endif
    }

    bool IsRunning() const {
#ifdef _WIN32
        return processInfo_.hProcess &&
            WaitForSingleObject(processInfo_.hProcess, 0) == WAIT_TIMEOUT;
#else
        return pid_ > 0 && kill(pid_, 0) == 0;
#endif
    }

    bool WriteAll(const std::vector<uint8_t>& bytes, std::string& error) {
#ifdef _WIN32
        size_t offset = 0;
        while (offset < bytes.size()) {
            DWORD written = 0;
            const DWORD remaining = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 65536));
            if (!WriteFile(stdinWrite_, bytes.data() + offset, remaining, &written, nullptr)) {
                error = "WriteFile failed: " + LastWin32Error();
                return false;
            }
            offset += written;
        }
        return true;
#else
        size_t offset = 0;
        while (offset < bytes.size()) {
            const ssize_t written = write(stdinFd_, bytes.data() + offset, bytes.size() - offset);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                error = "write failed: " + std::string(std::strerror(errno));
                return false;
            }
            offset += static_cast<size_t>(written);
        }
        return true;
#endif
    }

    bool ReadAvailable(std::chrono::milliseconds timeout, std::vector<uint8_t>& out, std::string& error) {
#ifdef _WIN32
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            DWORD available = 0;
            if (!PeekNamedPipe(stdoutRead_, nullptr, 0, nullptr, &available, nullptr)) {
                if (!IsRunning()) {
                    return true;
                }
                error = "PeekNamedPipe failed: " + LastWin32Error();
                return false;
            }

            if (available == 0) {
                Sleep(5);
                continue;
            }

            std::vector<uint8_t> buffer(std::min<DWORD>(available, 65536));
            DWORD bytesRead = 0;
            if (!ReadFile(stdoutRead_, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
                error = "ReadFile failed: " + LastWin32Error();
                return false;
            }
            out.insert(out.end(), buffer.begin(), buffer.begin() + bytesRead);
        }
        return true;
#else
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(stdoutFd_, &readSet);

        timeval tv;
        tv.tv_sec = static_cast<long>(timeout.count() / 1000);
        tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

        const int ready = select(stdoutFd_ + 1, &readSet, nullptr, nullptr, &tv);
        if (ready < 0) {
            if (errno == EINTR) {
                return true;
            }
            error = "select failed: " + std::string(std::strerror(errno));
            return false;
        }
        if (ready == 0) {
            return true;
        }

        uint8_t buffer[65536];
        while (true) {
            const ssize_t bytesRead = read(stdoutFd_, buffer, sizeof(buffer));
            if (bytesRead > 0) {
                out.insert(out.end(), buffer, buffer + bytesRead);
                continue;
            }
            if (bytesRead == 0) {
                return true;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            error = "read failed: " + std::string(std::strerror(errno));
            return false;
        }
#endif
    }

    void Terminate() {
#ifdef _WIN32
        if (processInfo_.hProcess) {
            if (IsRunning()) {
                TerminateProcess(processInfo_.hProcess, 0);
                WaitForSingleObject(processInfo_.hProcess, 1000);
            }
            CloseHandleIfValid(processInfo_.hProcess);
            CloseHandleIfValid(processInfo_.hThread);
            ZeroMemory(&processInfo_, sizeof(processInfo_));
        }
        CloseHandleIfValid(stdinWrite_);
        CloseHandleIfValid(stdoutRead_);
#else
        if (pid_ > 0) {
            kill(pid_, SIGTERM);
            int status = 0;
            waitpid(pid_, &status, 0);
            pid_ = -1;
        }
        if (stdinFd_ >= 0) {
            close(stdinFd_);
            stdinFd_ = -1;
        }
        if (stdoutFd_ >= 0) {
            close(stdoutFd_);
            stdoutFd_ = -1;
        }
#endif
    }

private:
#ifdef _WIN32
    static std::string LastWin32Error() {
        const DWORD code = GetLastError();
        if (code == 0) {
            return "unknown error";
        }

        char* message = nullptr;
        const DWORD length = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPSTR>(&message),
            0,
            nullptr);

        std::string out = length > 0 && message ? std::string(message, length) : std::to_string(code);
        if (message) {
            LocalFree(message);
        }
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
            out.pop_back();
        }
        return out;
    }

    static void CloseHandleIfValid(HANDLE& handle) {
        if (handle && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
        handle = nullptr;
    }

    static std::string QuoteWindowsArg(const std::string& arg) {
        if (arg.empty()) {
            return "\"\"";
        }

        const bool needsQuote = arg.find_first_of(" \t\"") != std::string::npos;
        if (!needsQuote) {
            return arg;
        }

        std::string out = "\"";
        size_t backslashes = 0;
        for (const char ch : arg) {
            if (ch == '\\') {
                ++backslashes;
            } else if (ch == '"') {
                out.append(backslashes * 2 + 1, '\\');
                out.push_back(ch);
                backslashes = 0;
            } else {
                out.append(backslashes, '\\');
                backslashes = 0;
                out.push_back(ch);
            }
        }
        out.append(backslashes * 2, '\\');
        out.push_back('"');
        return out;
    }

    static std::string BuildWindowsCommandLine(const std::vector<std::string>& args) {
        std::string out;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i != 0) {
                out.push_back(' ');
            }
            out += QuoteWindowsArg(args[i]);
        }
        return out;
    }

    PROCESS_INFORMATION processInfo_{};
    HANDLE stdinWrite_ = nullptr;
    HANDLE stdoutRead_ = nullptr;
#else
    static void ClosePipe(int pipeFds[2]) {
        if (pipeFds[0] >= 0) {
            close(pipeFds[0]);
        }
        if (pipeFds[1] >= 0) {
            close(pipeFds[1]);
        }
    }

    pid_t pid_ = -1;
    int stdinFd_ = -1;
    int stdoutFd_ = -1;
#endif
};

void ReplaceAll(std::string& value, const std::string& from, const std::string& to) {
    size_t offset = 0;
    while ((offset = value.find(from, offset)) != std::string::npos) {
        value.replace(offset, from.size(), to);
        offset += to.size();
    }
}

} // namespace

class NvimSurface::Impl {
public:
    bool Start(const NvimSurfaceConfig& config) {
        Shutdown();

        config_ = config;
        grid_.Resize(config.width, config.height);

        std::vector<std::string> args;
        args.push_back(config.executable);
        if (!config.loadUserConfig) {
            args.push_back("--clean");
        }
        args.push_back("--embed");
        args.push_back("-n");
        if (!config.filePath.empty()) {
            args.push_back(config.filePath);
        }

        if (!process_.Start(args, lastError_)) {
            return false;
        }

        const int64_t attachId = Request("nvim_ui_attach", {
            Int(config.width),
            Int(config.height),
            Map({
                {"rgb", Bool(true)},
                {"ext_linegrid", Bool(true)}
            })
        });

        if (attachId <= 0 || !WaitForResponse(attachId, config.requestTimeout)) {
            Shutdown();
            return false;
        }

        if (!Command("redraw!")) {
            Shutdown();
            return false;
        }

        Pump(std::chrono::milliseconds(250));
        return true;
    }

    void Shutdown() {
        if (process_.IsRunning()) {
            const int64_t quitId = Request("nvim_command", {String("qa!")});
            if (quitId > 0) {
                Pump(std::chrono::milliseconds(100));
            }
        }
        process_.Terminate();
        readBuffer_.clear();
        responses_.clear();
    }

    bool IsRunning() const {
        return process_.IsRunning();
    }

    bool Pump(std::chrono::milliseconds duration) {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < deadline) {
            if (!ReadAndHandle(std::chrono::milliseconds(50))) {
                return false;
            }
        }
        return true;
    }

    bool SendInput(const std::string& input) {
        const int64_t requestId = Request("nvim_input", {String(ExpandNvimInputTokens(input))});
        if (requestId <= 0 || !WaitForResponse(requestId, config_.requestTimeout)) {
            return false;
        }
        return Pump(std::chrono::milliseconds(500));
    }

    bool Command(const std::string& command) {
        const int64_t requestId = Request("nvim_command", {String(command)});
        if (requestId <= 0) {
            return false;
        }
        return WaitForResponse(requestId, config_.requestTimeout);
    }

    bool Resize(uint32_t width, uint32_t height) {
        config_.width = width;
        config_.height = height;

        const int64_t requestId = Request("nvim_ui_try_resize", {
            Int(width),
            Int(height)
        });
        if (requestId <= 0 || !WaitForResponse(requestId, config_.requestTimeout)) {
            return false;
        }
        return Pump(std::chrono::milliseconds(100));
    }

    NvimSurfaceSnapshot Snapshot() const {
        return grid_.Snapshot();
    }

    const std::string& LastError() const {
        return lastError_;
    }

private:
    int64_t Request(const std::string& method, std::vector<MsgpackValue> params) {
        const int64_t msgid = nextMsgId_++;
        const MsgpackValue message = Array({
            Int(0),
            Int(msgid),
            String(method),
            Array(std::move(params))
        });

        const auto bytes = codec_.Pack(message);
        if (!process_.WriteAll(bytes, lastError_)) {
            return -1;
        }
        return msgid;
    }

    bool WaitForResponse(int64_t requestId, std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (!ReadAndHandle(std::chrono::milliseconds(50))) {
                return false;
            }

            const auto it = responses_.find(requestId);
            if (it == responses_.end()) {
                continue;
            }

            const MsgpackValue error = it->second.first;
            responses_.erase(it);
            if (!IsNil(error)) {
                lastError_ = "nvim request failed: " + ToDebugString(error);
                return false;
            }
            return true;
        }

        lastError_ = "timed out waiting for nvim response " + std::to_string(requestId);
        return false;
    }

    bool ReadAndHandle(std::chrono::milliseconds timeout) {
        if (!process_.ReadAvailable(timeout, readBuffer_, lastError_)) {
            return false;
        }

        std::vector<MsgpackValue> messages;
        if (!codec_.UnpackStream(readBuffer_, messages, lastError_)) {
            return false;
        }

        for (const auto& message : messages) {
            HandleMessage(message);
        }

        return true;
    }

    void HandleMessage(const MsgpackValue& message) {
        if (!IsArray(message) || message.array.empty()) {
            return;
        }

        const int64_t kind = AsInt(message.array[0], -1);
        if (kind == 1 && message.array.size() >= 4) {
            responses_[AsInt(message.array[1])] = {message.array[2], message.array[3]};
            return;
        }

        if (kind != 2 || message.array.size() < 3) {
            return;
        }

        if (AsString(message.array[1]) != "redraw" || !IsArray(message.array[2])) {
            return;
        }

        for (const auto& event : message.array[2].array) {
            HandleRedrawEvent(event);
        }
    }

    void HandleRedrawEvent(const MsgpackValue& event) {
        if (!IsArray(event) || event.array.empty()) {
            return;
        }

        const std::string name = AsString(event.array[0]);
        for (size_t i = 1; i < event.array.size(); ++i) {
            DispatchRedraw(name, event.array[i]);
        }
    }

    void DispatchRedraw(const std::string& name, const MsgpackValue& args) {
        if (name == "grid_resize" && IsArray(args) && args.array.size() >= 3) {
            grid_.Resize(static_cast<uint32_t>(AsInt(args.array[1])),
                         static_cast<uint32_t>(AsInt(args.array[2])));
        } else if (name == "grid_clear" || name == "clear") {
            grid_.Clear();
        } else if (name == "grid_line" && IsArray(args) && args.array.size() >= 4) {
            grid_.ApplyLine(AsInt(args.array[1]), AsInt(args.array[2]), args.array[3]);
        } else if (name == "grid_cursor_goto" && IsArray(args) && args.array.size() >= 3) {
            grid_.SetCursor(static_cast<uint32_t>(AsInt(args.array[1])),
                            static_cast<uint32_t>(AsInt(args.array[2])));
        } else if (name == "grid_scroll" && IsArray(args) && args.array.size() >= 7) {
            grid_.Scroll(AsInt(args.array[1]),
                         AsInt(args.array[2]),
                         AsInt(args.array[3]),
                         AsInt(args.array[4]),
                         AsInt(args.array[5]),
                         AsInt(args.array[6]));
        }
    }

    NvimSurfaceConfig config_;
    ChildProcess process_;
    MsgpackCodec codec_;
    NvimGrid grid_;
    std::vector<uint8_t> readBuffer_;
    std::map<int64_t, std::pair<MsgpackValue, MsgpackValue>> responses_;
    int64_t nextMsgId_ = 1;
    std::string lastError_;
};

std::string NvimSurfaceSnapshot::ToPlainText() const {
    size_t rowCount = rows.size();
    while (rowCount > 0 && rows[rowCount - 1].empty()) {
        --rowCount;
    }

    std::string out;
    for (size_t i = 0; i < rowCount; ++i) {
        out += rows[i];
        out += '\n';
    }
    return out;
}

NvimSurface::NvimSurface()
    : impl_(new Impl())
{
}

NvimSurface::~NvimSurface() {
    Shutdown();
}

bool NvimSurface::Start(const NvimSurfaceConfig& config) {
    return impl_->Start(config);
}

void NvimSurface::Shutdown() {
    impl_->Shutdown();
}

bool NvimSurface::IsRunning() const {
    return impl_->IsRunning();
}

bool NvimSurface::Pump(std::chrono::milliseconds duration) {
    return impl_->Pump(duration);
}

bool NvimSurface::SendInput(const std::string& input) {
    return impl_->SendInput(input);
}

bool NvimSurface::Command(const std::string& command) {
    return impl_->Command(command);
}

bool NvimSurface::Resize(uint32_t width, uint32_t height) {
    return impl_->Resize(width, height);
}

NvimSurfaceSnapshot NvimSurface::Snapshot() const {
    return impl_->Snapshot();
}

const std::string& NvimSurface::LastError() const {
    return impl_->LastError();
}

std::string ExpandNvimInputTokens(const std::string& input) {
    std::string expanded = input;
    ReplaceAll(expanded, "<Esc>", "\x1b");
    ReplaceAll(expanded, "<CR>", "\r");
    ReplaceAll(expanded, "<Tab>", "\t");
    ReplaceAll(expanded, "<BS>", "\x7f");
    return expanded;
}

} // namespace Next
