---

# LVS Health Monitor (C++ – Parallel Ping + Sliding Window)

This project is a high-performance **LVS (Linux Virtual Server) backend health monitor** written in modern **C++17**.

Unlike shell scripts that ping servers sequentially, this program performs **true parallel health checks** using `std::thread`, allowing all backend servers to be monitored **simultaneously** with minimal delay.

The tool continuously evaluates packet loss for each backend server using a **60-second sliding window**, and automatically **adds or removes servers from LVS** using `ipvsadm` when packet loss crosses a configurable threshold.

---

## ✨ Features

### ✅ Parallel Pinging (Multithreaded)

Each backend server is monitored in its own thread for accurate real-time health checks.

### ✅ 60-Second Sliding Packet-Loss Window

Maintains a rolling history of loss samples (one per second) for stable up/down decisions.

### ✅ Automatic LVS Management

Automatically adjusts LVS based on health:

* Adds backend servers when healthy
* Removes backend servers if packet loss exceeds threshold

Uses:

* `ipvsadm -A` to create services
* `ipvsadm -a` to add servers
* `ipvsadm -d` to remove servers

### ✅ Fully Configurable

Easily modify:

* Backend server IPs
* TCP/UDP ports
* Loss threshold
* Sliding window size
* Ping timeout and behavior

---

## 📦 Requirements

### 1. C++ Compiler (g++)

You need a compiler with **C++17** support.

Check if installed:

```bash
g++ --version
```

Install if missing:

**Ubuntu / Debian:**

```bash
sudo apt update
sudo apt install g++
```

**Fedora:**

```bash
sudo dnf install gcc-c++
```

**CentOS / RHEL:**

```bash
sudo yum install gcc-c++
```

---

### 2. POSIX Threads (pthread)

Required for `std::thread`.

No manual installation is needed — just link using:

```bash
-pthread
```

---

### 3. ipvsadm (for LVS management)

Install it using:

**Ubuntu / Debian:**

```bash
sudo apt install ipvsadm
```

**Fedora / CentOS / RHEL:**

```bash
sudo dnf install ipvsadm
```

---

### 4. ping Utility

Required for packet-loss measurement.

Check:

```bash
ping -V
```

Install if needed:

```bash
sudo apt install iputils-ping
```

---

## 🛠️ Build Instructions

1. Clone the repository:

```bash
https://github.com/vriksterr/LVS-Health-Checker.git
cd LVS-Health-Checker
```

2. Compile the program:

```bash
g++ -std=c++17 -pthread lvs_monitor.cpp -o lvs_monitor
```

3. Run it
   Root privileges are required because `ipvsadm` modifies kernel LVS tables:

```bash
sudo ./lvs_monitor
```

---
# 🔒 Hardening C++ Code Against Reverse Engineering

*(Markdown-Compatible Version)*

You can make your C++ code significantly harder to reverse-engineer, but **no native binary can be fully protected** from a determined attacker.
You can only make reversing extremely time-consuming.

This document explains all the techniques you can apply.

---

## ✅ 1. Strip Symbols

Remove all function/variable names, RTTI, and debug info:

```bash
g++ -O2 -s -fvisibility=hidden source.cpp -o lvs_monitor
strip lvs_monitor
```

---

## ✅ 2. Use Aggressive Compiler Optimizations

Make reverse engineering harder:

```bash
-O3 -funroll-loops -fno-rtti -fno-exceptions
```

---

## ✅ 3. Encrypt Strings

Do **not** leave plaintext IPs, shell commands, or constants in the binary.

Basic XOR runtime-decryption wrapper:

```cpp
#define XOR_KEY 0xA4

std::string decrypt(const unsigned char* data, size_t len) {
    std::string s; s.reserve(len);
    for (size_t i = 0; i < len; i++) s.push_back(data[i] ^ XOR_KEY);
    return s;
}
```

You would store encrypted strings like:

```cpp
static const unsigned char ENC_PING[] = { /* encrypted-bytes */ };
```

---

## ✅ 4. Control Flow Flattening

Replace readable functions with a **state machine**:

```cpp
void monitor_server_obf(const std::string& srv) {
    int s = 0;
    while (true) {
        switch (s) {
            case 0: /* do ping */  s = 2; break;
            case 2: /* calc avg */ s = 4; break;
            case 4: /* LVS ops */  s = 0; break;
        }
    }
}
```

This breaks common decompiler output patterns.

---

## ✅ 5. Anti-Debugging Tricks

Add debugger detection:

```cpp
#include <sys/ptrace.h>

bool detect_debugger() {
    if (ptrace(PTRACE_TRACEME, 0, 1, 0) < 0)
        return true;
    return false;
}
```

Other checks:

* detect timing slowdown
* check `/proc/self/status` for `TracerPid`
* scan memory for breakpoints (`0xCC`)

---

## ✅ 6. Anti-VM / Anti-Sandbox Checks

Check:

* CPU vendor strings (`QEMU`, `KVM`, `VMware`)
* Low RAM (<2GB)
* Files like `/sys/class/dmi/id/product_name`
* Modules like `vboxsf`, `vmhgfs`

Reverse engineers often use VMs → this disrupts analysis.

---

## ✅ 7. Binary Integrity (Self-Hashing)

Prevent patching:

```cpp
std::string hash_self();
if (hash_self() != EXPECTED_HASH) exit(1);
```

The program terminates if modified.

---

## ✅ 8. Add Junk Code

Insert misleading computations:

```cpp
volatile int x = rand() ^ time(nullptr);
```

Makes it harder for decompilers to reconstruct logic.

---

## ✅ 9. Load Critical Logic as Encrypted Blob

Encrypt functions using AES and load at runtime:

* decrypt into memory
* call via function pointer
* overwrite memory with zeros after execution

Reverse engineering becomes extremely difficult.

---

## ✅ 10. Use OLLVM (Recommended)

**OLLVM** (Obfuscator-LLVM) provides:

* Control Flow Flattening
* Bogus Control Flow
* Instruction Substitution
* String Encryption

Compile with:

```bash
clang++ -mllvm -fla -mllvm -sub -mllvm -bcf source.cpp -o output
```
## 📘 How It Works

### 1. Initialization

Each server gets a dedicated sliding-window loss buffer.

### 2. Ping Thread

Every server runs inside its own thread:

* Sends a ping every second
* Extracts packet-loss percentage
* Updates the 60-second loss history

### 3. Health Evaluation

* If average loss ≥ threshold → marked **DOWN** → removed from LVS
* If average loss < threshold → marked **UP** → added back to LVS

### 4. Continuous Loop

Runs forever, adjusting LVS based on real-time health.

---

## 📄 License

(Choose one: MIT, Apache, GPL, etc.)

---

If you'd like, I can also generate:

* A complete `systemd` service unit
* A `config.json` + parser
* A better directory layout (`src/`, `include/`, `build/`)
* A log file system

Just tell me!
