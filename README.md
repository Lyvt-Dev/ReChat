# ReChat

Native C++17 / Qt 6 desktop messenger for the same local network. Dark Telegram-inspired layout, violet Discord-inspired accents, original SVG logo. No central server, cloud account or internet connection needed at runtime. Each app listens for direct connections itself.

## Download & run

Open **Actions → Build ReChat → latest successful run → Artifacts**. Download `ReChat-Windows`, extract the entire ZIP and run `ReChat.exe`. Keep the bundled DLLs and plugin folders beside it. Allow ReChat through the firewall on **private networks**. Linux artifact requires Qt 6.8 runtime libraries (or build against your distribution's Qt).

Run ReChat on two devices on the same WLAN / LAN. Claim a username, wait a few seconds for discovery, select a contact and send a message. No demo contacts or simulated delivery: outgoing messages enter history only after the other app acknowledges receipt. Failed delivery keeps the text in the composer. ReChat must be running on both devices; offline delivery is not supported.

## Build from source

Install CMake 3.21+, a C++17 compiler and Qt 6 with Widgets, Network and SVG (on Debian/Ubuntu: `cmake qt6-base-dev qt6-svg-dev`).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Run `build/ReChat` on Linux or `build/Release/ReChat.exe` on Windows. Set `CMAKE_PREFIX_PATH` to your Qt installation if necessary. GitHub Actions builds Windows and Linux on every push and runs a username validation smoke test.

## Networking & username claims

IPv4 multicast `239.255.42.99:45454/UDP` discovers peers on active multicast-capable interfaces. Presence refreshes every 2 seconds, expires after 9 seconds; messages use direct TCP on an ephemeral port. Guest Wi-Fi isolation, blocked multicast and firewalls can prevent discovery or delivery. There is no router port forwarding or internet relay.

Usernames are lowercase, 3–24 characters (`a-z`, digits, underscore), beginning with a letter. Claims last while discoverable and online. Simultaneous duplicate claims resolve by device UUID order; the losing device must choose again. Claims are **not global accounts**, do not reserve names while offline, and cannot guarantee uniqueness across partitioned networks. Device UUID and name persist locally. Copying profile data to a second device duplicates its identity.

## Privacy & current limits

This initial version is for trusted private LANs: discovery and chat traffic are unencrypted and peers are not cryptographically authenticated. Do not use it for sensitive conversations or on untrusted networks. A username is not proof of identity. No E2E encryption, attachments, groups, calls, background notifications or mobile builds yet.

Chat history is stored as plaintext `history.json` in Qt's per-user application data directory; username and device identity use QSettings. Incoming text is HTML-escaped and frame/text sizes and connection lifetimes are bounded. The receiver acknowledges in-memory receipt; disk-write errors are shown locally. Automated checks currently cover username validation only; a real two-device LAN acceptance test is still needed.
