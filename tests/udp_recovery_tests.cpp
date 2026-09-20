// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Timing lock for the case the player actually hits: they launch Subliminal
// while the previous game is still running and holding the tracker port, then
// remember, alt-tab out and close it. Nothing in the mod is restarted, so the
// only thing that can reclaim the port is the receiver's supervisor thread -
// and the only way to know how long that takes is to measure it.
//
// This runs the mod's own Session over the real UdpReceiver against a real
// loopback socket, with a sender pushing 60Hz OpenTrack packets throughout, and
// records per trial:
//
//   bind - milliseconds from the competing process closing its socket to
//          IsRunning() going true
//   pose - milliseconds from the same instant to Session::Update() publishing
//          a rotation, which is the point the camera starts moving again
//
// The release instant is moved through a full retry period across the trials so
// the samples land at different phases of the supervisor's cycle; the worst case
// over them is then a measurement of the retry cadence from outside, not a
// restatement of the constant.

#include "session.h"

#include "cameraunlock/protocol/udp_socket.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

// Deliberately outside cameraunlock-core's own receiver test range (1426x) so
// the two suites can run at the same time.
constexpr uint16_t kTrackerPort = 14281;
constexpr uint16_t kSenderPort = 14282;

constexpr int kTrials = 6;
constexpr float kFrameSeconds = 1.0f / 60.0f;

using Clock = std::chrono::steady_clock;

int64_t MsSince(Clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
}

// What the OS itself calls this error code. The bind failure line has to carry
// these words rather than a guess at the cause: a bind fails for reasons that
// are not a port conflict, and a line naming the wrong one sends the player
// hunting an application that is not running.
std::string OsErrorText(int code) {
    char* buffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code), 0, reinterpret_cast<char*>(&buffer), 0, nullptr);
    std::string text;
    if (buffer) {
        text.assign(buffer, length);
        LocalFree(buffer);
    }
    const size_t lastReal = text.find_last_not_of(" \r\n");
    text.erase(lastReal == std::string::npos ? 0 : lastReal + 1);
    return text;
}

// The other game, still running and still bound to the tracker port. Raw
// winsock rather than UdpSocket because this side is not our code.
SOCKET HoldPort(uint16_t port) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

size_t BuildPacket(uint8_t out[48], double yaw, double pitch, double roll) {
    const double zero = 0.0;
    std::memcpy(out + 0, &zero, sizeof(double));
    std::memcpy(out + 8, &zero, sizeof(double));
    std::memcpy(out + 16, &zero, sizeof(double));
    std::memcpy(out + 24, &yaw, sizeof(double));
    std::memcpy(out + 32, &pitch, sizeof(double));
    std::memcpy(out + 40, &roll, sizeof(double));
    return 48;
}

// A tracker app that keeps sending regardless of who is listening, which is what
// OpenTrack and the phone apps do. The yaw walks a fraction of a degree per
// packet so the stream never looks like a tracker that has frozen.
class TrackerSender {
public:
    bool Start() {
        if (!m_socket.Open(kSenderPort)) return false;
        m_running = true;
        m_thread = std::thread([this] {
            sockaddr_in dest = {};
            dest.sin_family = AF_INET;
            dest.sin_port = htons(kTrackerPort);
            inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);
            double yaw = 0.0;
            uint8_t packet[48];
            while (m_running.load(std::memory_order_relaxed)) {
                yaw += 0.05;
                if (yaw > 5.0) yaw = 0.0;
                const size_t length = BuildPacket(packet, yaw, 1.0, 2.0);
                sendto(m_socket.GetHandle(), reinterpret_cast<const char*>(packet),
                       static_cast<int>(length), 0,
                       reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        });
        return true;
    }

    ~TrackerSender() {
        m_running = false;
        if (m_thread.joinable()) m_thread.join();
        m_socket.Close();
    }

private:
    cameraunlock::UdpSocket m_socket;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
};

struct Trial {
    int64_t bindMs = -1;
    int64_t poseMs = -1;
};

int64_t WorstOf(const std::vector<Trial>& trials, int64_t Trial::*field) {
    int64_t worst = -1;
    for (const Trial& t : trials) worst = std::max(worst, t.*field);
    return worst;
}

}  // namespace

int main() {
    using cameraunlock::UdpReceiver;

    std::cout << "UDP port recovery timing (mod Session over UdpReceiver):\n";

    TrackerSender tracker;
    if (!tracker.Start()) {
        Check(false, "tracker sender opens its loopback socket");
        return 1;
    }

    const std::string inUseText = OsErrorText(WSAEADDRINUSE);
    std::vector<Trial> trials;

    for (int i = 0; i < kTrials; ++i) {
        SOCKET occupier = HoldPort(kTrackerPort);
        if (occupier == INVALID_SOCKET) {
            Check(false, "the previous game holds the tracker port");
            return 1;
        }

        UdpReceiver receiver;
        std::string log;
        receiver.SetLog([&](const std::string& message) { log += message + "\n"; });

        const bool bound = receiver.Start(kTrackerPort);
        subliminal_ht::Session session(receiver);

        if (i == 0) {
            Check(!bound, "Start reports the port as unavailable");
            Check(receiver.IsRetrying(), "the supervisor is retrying");
            Check(!receiver.IsRunning(), "no receive thread while the port is held");
            Check(log.find("bind failed with error " + std::to_string(WSAEADDRINUSE)) !=
                      std::string::npos,
                  "the log names the failing call and the code the OS returned");
            Check(!inUseText.empty() && log.find(inUseText) != std::string::npos,
                  "the log quotes the OS's own words for it: " + inUseText);
            std::cout << "  bind failure line: " << log.substr(0, log.find('\n')) << "\n";
        }

        // Release at a different phase of the retry cycle every trial, so the
        // worst case across them measures the cadence rather than one lucky
        // alignment with it.
        std::this_thread::sleep_for(
            std::chrono::milliseconds((i * UdpReceiver::kRetryIntervalMs) / kTrials));

        const Clock::time_point released = Clock::now();
        closesocket(occupier);

        Trial trial;
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        while (MsSince(released) < 5000) {
            if (trial.bindMs < 0 && receiver.IsRunning()) trial.bindMs = MsSince(released);
            if (session.Update(kFrameSeconds) && session.GetRotation(yaw, pitch, roll)) {
                trial.poseMs = MsSince(released);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // This is the only end-to-end packet path in the suite - socket, parser,
        // interpolator, processor - and without these two lines it asserted only
        // that SOME pose arrived. The sender puts distinct constants in the pitch
        // and roll fields (1.0 and 2.0) precisely so a transposed pair is
        // visible; the tolerance is wide because the interpolator is still
        // converging on the first sample, and 0.5 is far tighter than the gap
        // between the two values.
        if (trial.poseMs >= 0) {
            Check(std::fabs(pitch - 1.0f) < 0.5f,
                  "trial " + std::to_string(i + 1) + ": pitch decodes from the pitch field");
            Check(std::fabs(roll - 2.0f) < 0.5f,
                  "trial " + std::to_string(i + 1) + ": roll decodes from the roll field");
        }

        std::cout << "  trial " << (i + 1) << ": bind " << trial.bindMs
                  << "ms, first pose to the camera " << trial.poseMs << "ms\n";
        trials.push_back(trial);

        receiver.Stop();
        Check(!receiver.IsRunning() && !receiver.IsRetrying(),
              "trial " + std::to_string(i + 1) + ": Stop tears the receiver down");
    }

    const int64_t worstBind = WorstOf(trials, &Trial::bindMs);
    const int64_t worstPose = WorstOf(trials, &Trial::poseMs);
    std::cout << "  worst case over " << kTrials << " trials: bind " << worstBind
              << "ms, pose " << worstPose << "ms\n";

    for (size_t i = 0; i < trials.size(); ++i) {
        Check(trials[i].bindMs >= 0,
              "trial " + std::to_string(i + 1) + ": the port is reclaimed with no restart");
        Check(trials[i].poseMs >= 0,
              "trial " + std::to_string(i + 1) + ": tracker data reaches the camera again");
    }

    // One retry interval plus a supervisor tick is the whole budget: the
    // supervisor wakes every 100ms and rebinds on the first wake a full interval
    // past the last attempt. The slack covers a loaded scheduler, not a second
    // retry cycle - above this the cadence itself has changed.
    const int64_t budget = UdpReceiver::kRetryIntervalMs + 100 + 250;
    Check(worstBind >= 0 && worstBind <= budget,
          "worst bind latency " + std::to_string(worstBind) + "ms is within " +
              std::to_string(budget) + "ms");
    Check(worstPose >= 0 && worstPose <= budget + 100,
          "worst pose latency " + std::to_string(worstPose) + "ms is within " +
              std::to_string(budget + 100) + "ms");

    if (g_failures == 0) {
        std::cout << "UDP recovery tests: all passed\n";
    } else {
        std::cout << "UDP recovery tests: " << g_failures << " failure(s)\n";
    }
    return g_failures == 0 ? 0 : 1;
}
