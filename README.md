# Low-Latency-HFT-Engine-C
**Ultra-low latency HFT engine for Interactive Brokers (TWS/Gateway) developed in pure C and Win32 API.**

This engine is designed for high-frequency scalping strategies where microsecond execution is the difference between alpha and slippage. It bypasses common high-level abstractions to provide direct hardware and kernel-level control.

## 🚀 Key Performance Features
*   **Zero-Copy Stream Parsing:** Advanced TCP stream reconstruction logic that handles fragmented packets using raw pointer arithmetic and memory accumulators.
*   **Non-Blocking Spin-loop (100% CPU Polling):** Implementation of `FIONBIO` mode to eliminate Kernel suspension latency (`WSAEWOULDBLOCK`).
*   **Win32 Kernel Shielding:** 
    *   `REALTIME_PRIORITY_CLASS`: Absolute CPU priority.
    *   `SetProcessAffinityMask`: Dedicated Core 0 Pinning to eliminate context switching jitter.
    *   `SetProcessWorkingSetSize`: RAM locking to prevent Page Faults/Swap to Disk.
*   **Async Multi-threaded Logging:** Dedicated servant thread for disk I/O using Critical Sections, ensuring the trading hot-path is never blocked by slow disk operations.
*   **TCP_NODELAY (Nagle's Algorithm Bypass):** Immediate packet transmission for both Simulator and Robot.

## 🛠 Tech Stack
- **Language:** C (ISO C99)
- **API:** Win32 (Winsock2)
- **Target Platform:** Windows (Optimized for Low-Latency VPS)
- **Architecture:** x86_64

## 📊 Benchmarking & Stress Testing
Included is a **High-Fidelity Market Simulator** that mimics real-world Interactive Brokers (IB) behavior:
- Gaussian Noise price generation (Geometric Brownian Motion).
- Microsecond timestamp precision (`QueryPerformanceCounter`).
- Random packet fragmentation and network jitter simulation.
- **Result:** Stable latencies between **7.5μs and 12μs** under heavy burst conditions.

## 📂 Configuration
The engine is fully parameterized via `config.ini`, allowing dynamic adjustment of:
- Target IP/Port
- Asset Symbol & Quantity
- Dynamic Spread Thresholds

---
*Disclaimer: This is high-performance financial software. Use at your own risk. Past performance does not guarantee future results.*
