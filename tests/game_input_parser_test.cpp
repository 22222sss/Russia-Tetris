#include "TetrisGame/GameInputParser.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* name) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", name);
    if (!ok) ++failures;
}
std::vector<GameInputCommand> drain(std::string& pending) {
    std::vector<GameInputCommand> commands;
    GameInputCommand command{};
    while (GameInputParser::next(pending, command)) commands.push_back(command);
    return commands;
}
bool equals(const std::vector<GameInputCommand>& actual,
            std::initializer_list<GameInputCommand> expected) {
    return actual == std::vector<GameInputCommand>(expected);
}
}

int main() {
    using C = GameInputCommand;
    {
        std::string pending;
        check(GameInputParser::append(pending, "\x1b[D", 3), "append one complete arrow");
        check(equals(drain(pending), {C::MoveLeft}) && pending.empty(),
              "one complete arrow produces exactly one left command");
    }
    {
        std::string pending;
        GameInputParser::append(pending, "\x1b", 1);
        check(drain(pending).empty() && pending == "\x1b", "lone ESC is retained");
        GameInputParser::append(pending, "[", 1);
        check(drain(pending).empty() && pending == "\x1b[", "partial CSI prefix is retained");
        GameInputParser::append(pending, "D", 1);
        check(equals(drain(pending), {C::MoveLeft}) && pending.empty(),
              "split ESC [ D becomes one left command");
    }
    {
        std::string pending;
        const std::string bytes = "\x1b[B\x1b[D\x1b[C rR ";
        GameInputParser::append(pending, bytes.data(), bytes.size());
        check(equals(drain(pending), {C::MoveDown, C::MoveLeft, C::MoveRight,
                                      C::Rotate, C::Repaint, C::Repaint, C::Rotate}) && pending.empty(),
              "coalesced arrows and printable controls preserve order");
    }
    {
        std::string pending;
        GameInputParser::append(pending, "\x1b[", 2);
        const char secondChunk[] = {'C', '\x1b', '['};
        GameInputParser::append(pending, secondChunk, sizeof(secondChunk));
        check(equals(drain(pending), {C::MoveRight}) && pending == "\x1b[",
              "complete command is emitted while trailing partial command remains");
        GameInputParser::append(pending, "B", 1);
        check(equals(drain(pending), {C::MoveDown}) && pending.empty(),
              "trailing command completes on the next read");
    }
    {
        std::string pending = "\x1b[1;5D\x1b[A\x1b[H";
        check(equals(drain(pending), {C::MoveLeft}) && pending.empty(),
              "modified left arrow works and unsupported CSI is ignored");
    }
    {
        std::string playerA;
        std::string playerB;
        GameInputParser::append(playerA, "\x1b", 1);
        GameInputParser::append(playerB, "\x1b[C", 3);
        check(equals(drain(playerB), {C::MoveRight}) && playerA == "\x1b",
              "one player's partial input does not affect another player's buffer");
        GameInputParser::append(playerA, "[D", 2);
        check(equals(drain(playerA), {C::MoveLeft}),
              "interleaved per-player input completes independently");
    }
    {
        std::string pending = "x\r\n\t\x1b[Z";
        check(drain(pending).empty() && pending.empty(),
              "unsupported keys and terminal line endings are consumed safely");
    }
    {
        const char malformed[] = {'\x1b', '[', '\0', '\x1b', '[', 'D'};
        std::string pending;
        GameInputParser::append(pending, malformed, sizeof(malformed));
        check(equals(drain(pending), {C::MoveLeft}) && pending.empty(),
              "malformed CSI followed by a valid key is handled safely");
    }
    {
        std::string pending = "\x1b[" + std::string(GameInputParser::kMaxCsiSequenceBytes, '1');
        check(drain(pending).empty() && pending.empty(),
              "overlong unterminated CSI is discarded");
    }
    {
        std::string pending(GameInputParser::kMaxBufferedBytes - 2, 'x');
        const char extra[] = "abcd";
        check(!GameInputParser::append(pending, extra, sizeof(extra) - 1) && pending.empty(),
              "input buffer overflow clears pending bytes and reports failure");
        check(!GameInputParser::append(pending, nullptr, 1) && pending.empty(),
              "null input pointer with nonzero length is rejected");
        check(GameInputParser::append(pending, nullptr, 0),
              "zero-length append is a no-op");
    }
    {
        std::string pending;
        std::size_t count = 0;
        const std::string burst = "\x1b[D\x1b[C ";
        std::string block;
        for (int i = 0; i < 200; ++i) block += burst;
        for (std::size_t offset = 0; offset < block.size(); offset += 37) {
            const std::size_t length = std::min<std::size_t>(37, block.size() - offset);
            if (!GameInputParser::append(pending, block.data() + offset, length)) {
                check(false, "append burst chunk");
                break;
            }
            count += drain(pending).size();
        }
        check(count == 600 && pending.empty(),
              "large burst split across arbitrary chunks yields every command");
    }
    if (failures != 0) {
        std::printf("%d game input parser test(s) failed\n", failures);
        return 1;
    }
    std::puts("game input parser tests all passed");
    return 0;
}
