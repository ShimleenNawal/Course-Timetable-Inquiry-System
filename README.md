# Timetable inquiry system (DCN Assignment 2 | Group 16: NADEEM Muhammad Ahmad, KHAN Maryam, NAWAL Shimleen, OTHOI Farhat Lamisa)

Course timetable lookup over TCP: one C++ server, CSV-backed storage, and two clients (console C++ and WPF C#) that speak the same text protocol.


---

## Setup (Windows)

**What you need**

- **System:** use a **64-bit** Windows install (the shipped `Client.exe` / `Server.exe` and the .NET 7 WPF build target 64-bit Windows).
- **Server + CLI:** a C++17 toolchain with Winsock (e.g. Visual Studio, or MinGW-w64 g++ as we have been using).
- **GUI:** [.NET 7 SDK](https://dotnet.microsoft.com/download/dotnet/7.0) (Windows desktop / WPF).

**Data file**

- Default database file is `courses_bnbu.csv` in the project folder (see `DEFAULT_DB_FILE` in `database.h`).
- Optional: set environment variable `DATACOM_DB_FILE` to the **full path** of another CSV if you want the server to load a copy elsewhere.

**Build the C++ server**

From the project root (where `Server.cpp` and `database.cpp` live):

```text
g++ -std=c++17 -O2 -Wall -Wextra -o Server.exe Server.cpp database.cpp -lws2_32
```

Stop any old `Server.exe` before rebuilding, or Windows may block overwriting it.

**Build the C++ CLI client**

```text
g++ -std=c++17 -O2 -Wall -Wextra -o Client.exe Client.cpp -lws2_32
```

**Build / run the GUI**

```text
cd CSharpClient\TimetableClientGui
dotnet build -c Release
dotnet run -c Release
```

Or open the folder in Visual Studio / Rider and run from there.

**Run order**

1. Start `Server.exe` (default port **50000**).
2. Start `Client.exe` and/or the GUI; point the GUI at host `127.0.0.1` (or your machine) and port `50000` if it is not already filled in.
3. **GUI only:** click **CONNECT** before running any query, search, or admin action. The status line should show a successful connect; otherwise commands will not reach the server.

**Firewall**

- If the client and server run on different machines, or Windows Firewall prompts you, allow **`Server.exe`** (inbound, listening port) and **`Client.exe`** / the GUI through the firewall. You can add explicit **Inbound** and **Outbound** rules in *Windows Defender Firewall with Advanced Security* if automatic prompts do not appear.

**Prebuilt binaries (no recompile required)**

- The project is meant to work **out of the box** with the included `Client.exe` and `Server.exe`; you do not need to recompile unless you change the C++ sources.
- If you **do** recompile from `Client.cpp` / `Server.cpp`, name the outputs exactly **`Client.exe`** and **`Server.exe`** (capital **C** and **S**). The build commands in this README already use those names; mismatched casing (e.g. `client.exe`) can break scripts, shortcuts, or firewall rules that expect the canonical names.

**Logs**

- `server.log` – connection lines, received commands, auth, and high-level query notes from the server.
- `database.log` – loader/cache messages and DB-layer search/CRUD traces (written by `database.cpp`).

---

## Objectives

- Give students a simple way to **query** timetable rows (by code, instructor, or view all) over the network.
- Let an **admin** log in and **add / update / delete** rows, with changes persisted to CSV.
- Keep **one agreed protocol** (`protocol.h`) so the C++ and C# clients stay in sync with the server.

---

## Tech stack

| Piece | Technology |
|--------|------------|
| Server | C++, Winsock TCP, one thread per connected client |
| Shared rules | `protocol.h` (commands, responses, admin field names) |
| Storage | CSV (`Course` rows: code, title, section, instructor, day, time, credits, offering unit), in-memory cache + mutex |
| CLI | C++, Winsock, menu-driven |
| GUI | C# / .NET 7, WPF, async TCP (`ProtocolClient.cs`) |

---

## How this lines up with the usual rubric

- **TCP sockets, client and server:** `Server.cpp` listens and accepts; `Client.cpp` and the GUI connect and send line-based messages ending in newline.
- **Application-level protocol:** fixed verbs in `protocol.h` (`QUERY`, `SEARCH_INSTRUCTOR`, `VIEW_ALL`, `LOGIN`, `ADD`, …) and `RESULT` / `END` for multi-row replies.
- **Concurrency:** each accepted socket is handled in its own `std::thread` (`Server.cpp`); the database side uses a mutex around cache and file resolution so concurrent clients do not corrupt the CSV.
- **Persistence:** courses are loaded from CSV, validated, and flushed back on changes; startup logs show resolved path and row count.
- **Role separation:** student commands work without login; `ADD` / `UPDATE` / `DELETE` return not authorized unless `LOGIN` succeeded as admin (credentials in `protocol.h` for now).
- **Logging / observability:** `server.log` plus database logging; enough to show who asked for what after a demo.

---

## Bonus / “stretch” material

The assignment PDF usually labels a few optional marks. In our tree, the **extra plumbing** mostly lives in `database.cpp` / `database.h`:

- **Richer data layer:** JSON export (`exportToJSON`), timestamped **`.bak_*` file backup** (`backupDatabase`), extra search helpers (`searchByTimeSlot`, `searchByOfferingUnit`), `getUniqueInstructors`, row counts, cache invalidation / shutdown flush.
- **Robust CSV handling:** quoted fields, optional BN BU-style wide rows for `courses_bnbu.csv`, dedupe keys on load, size guard on the DB file.
- **Operator comfort:** server sets UTF-8 console code page on Windows so log output and CSV text are less likely to turn into mojibake on the terminal.

Not all of those helpers are wired to new TCP commands yet; if the PDF asks for a specific *wire* feature, either add a command in `Server.cpp` that calls the matching `database` function or point your write-up at the function you already have and show a one-off call / test.

---

## CLI vs GUI and how logs tell them apart

Both clients send an initial **`HELLO`** line with a short name right after connect (examples below).

- C++ CLI: `HELLO CLI` (`Client.cpp`).
- WPF GUI: `HELLO GUI` (`ProtocolClient.cs` on connect).

The server starts each session as `Unknown@127.0.0.1:54321` style (IP plus ephemeral port), then swaps the `Unknown` part for whatever came after `HELLO ` (so you see `CLI@127.0.0.1:54321` next to `GUI@127.0.0.1:54322` if two windows are open). Later log lines for that socket reuse the same id until disconnect.

So in `server.log` you can tell **which app** was used (CLI vs GUI from the name) and **which connection** if two windows are open at once (ephemeral **TCP port** in the id). Same idea if one machine hits the server from two terminals: different ports, different lines.

---

## Summary

We ship a small but complete path: **server stays up**, **CSV is the source of truth**, **two clients** share one protocol, and **logs** stay readable because each client introduces itself with `HELLO` and the server keeps the IP:port in the id. Clone or zip the project, build as above, drop your timetable CSV in place (or point `DATACOM_DB_FILE` at it), and you are ready for a demo or submission video.

