#pragma once

#include <cstddef>
#include <string>

enum class GameInputCommand {
    MoveDown,
    MoveLeft,
    MoveRight,
    Rotate,
    Repaint
};

// Incremental parser for terminal key bytes carried by a TCP byte stream.
// The pending buffer belongs to one connection and may end in a partial
// ESC/CSI sequence; parsing resumes after the next recv() appends more bytes.
class GameInputParser {
public:
    static constexpr std::size_t kMaxBufferedBytes = 4096;
    static constexpr std::size_t kMaxCsiSequenceBytes = 32;

    static bool append(std::string& pending, const char* bytes, std::size_t length)
    {
        if (length == 0) return true;
        if (bytes == nullptr || length > kMaxBufferedBytes ||
            pending.size() > kMaxBufferedBytes - length) {
            pending.clear();
            return false;
        }
        pending.append(bytes, length);
        return true;
    }

    // Returns one recognized command. Irrelevant bytes and unsupported
    // terminal sequences are consumed; incomplete ESC/CSI sequences remain
    // buffered and return false until more bytes arrive.
    static bool next(std::string& pending, GameInputCommand& command)
    {
        while (!pending.empty()) {
            const unsigned char first = static_cast<unsigned char>(pending[0]);
            if (first == 0x1b) {
                if (pending.size() == 1) return false;
                if (pending[1] != '[') {
                    pending.erase(0, 1);
                    continue;
                }

                std::size_t finalPos = 2;
                bool malformed = false;
                while (finalPos < pending.size()) {
                    const unsigned char ch = static_cast<unsigned char>(pending[finalPos]);
                    if (ch >= 0x40 && ch <= 0x7e) break;
                    if (ch < 0x20 || ch > 0x3f) {
                        pending.erase(0, 1);
                        malformed = true;
                        break;
                    }
                    ++finalPos;
                }
                if (malformed) continue;

                if (finalPos == pending.size()) {
                    if (pending.size() > kMaxCsiSequenceBytes) pending.clear();
                    return false;
                }

                const bool sequenceWithinLimit = finalPos + 1 <= kMaxCsiSequenceBytes;
                const unsigned char finalByte = static_cast<unsigned char>(pending[finalPos]);
                pending.erase(0, finalPos + 1);
                if (!sequenceWithinLimit) continue;

                switch (finalByte) {
                    case 'B': command = GameInputCommand::MoveDown; return true;
                    case 'D': command = GameInputCommand::MoveLeft; return true;
                    case 'C': command = GameInputCommand::MoveRight; return true;
                    default: continue; // Up arrow and unsupported CSI commands.
                }
            }

            const char key = pending[0];
            pending.erase(0, 1);
            switch (key) {
                case ' ': command = GameInputCommand::Rotate; return true;
                case 'r':
                case 'R': command = GameInputCommand::Repaint; return true;
                default: continue; // CR/LF, unsupported keys and other terminal bytes.
            }
        }
        return false;
    }
};
