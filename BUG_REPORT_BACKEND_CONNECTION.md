# Bug Report: AsyncHttpClient Backend Connection Issue

## Problem Summary
terrain_editor shows "AI Backend not connected" even when the backend server is running and healthy. The connection never establishes regardless of restart order.

## Environment
- **OS:** Linux (Pop!_OS)
- **Backend:** Python FastAPI server on `http://localhost:8080`
- **Client:** C++ AsyncHttpClient in terrain_editor
- **Backend Status:** Confirmed healthy via `curl http://localhost:8080/health`

## Symptoms
1. terrain_editor starts and logs "AI Backend not available"
2. Backend server IS running and responds to curl requests
3. Restarting terrain_editor (even after backend is confirmed running) doesn't help
4. In-game conversation UI shows "(AI backend not connected)"

## Relevant Files

### AsyncHttpClient.hpp
Location: `src/Network/AsyncHttpClient.hpp`

Key code:
```cpp
bool isConnected() const { return m_connected; }
std::atomic<bool> m_connected{false};
```

### AsyncHttpClient.cpp
Location: `src/Network/AsyncHttpClient.cpp`

Connection logic in `executeRequest()`:
```cpp
if (result) {
    response.success = (result->status >= 200 && result->status < 300);
    response.statusCode = result->status;
    response.body = result->body;
    m_connected = true;  // Only set true on success
} else {
    response.success = false;
    response.error = "Connection failed";
    m_connected = false;  // Set false on failure
}
```

### terrain_editor/main.cpp
Startup code (around line 157):
```cpp
m_httpClient = std::make_unique<AsyncHttpClient>("http://localhost:8080");
m_httpClient->start();
m_httpClient->checkHealth([](const AsyncHttpClient::Response& resp) {
    if (resp.success) {
        std::cout << "AI Backend connected successfully\n";
    } else {
        std::cout << "AI Backend not available (start backend/server.py)\n";
    }
});
```

Usage check (around line 3400 and 6785):
```cpp
if (m_httpClient && m_httpClient->isConnected()) {
    // Send chat message...
}
```

## Root Cause Analysis

The `m_connected` flag is ONLY set to `true` inside `executeRequest()` when a request succeeds. However:

1. The `checkHealth()` call is async - it queues a request
2. The callback runs later when the worker thread processes it
3. If the request fails OR the response hasn't arrived yet, `m_connected` stays `false`
4. There's no retry mechanism - once marked disconnected, it stays that way

**Possible issues:**
1. Worker thread might not be processing requests properly
2. IPv6 vs IPv4 resolution issue (curl shows it tries ::1 first)
3. Request timing - main thread might check `isConnected()` before health check completes
4. cpp-httplib configuration issue

## Debugging Steps Needed

1. Add logging to `workerThread()` to confirm it's running and processing requests
2. Add logging to `executeRequest()` to see if requests are being made
3. Check if `m_requestQueue` is actually receiving the health check request
4. Verify cpp-httplib is connecting correctly (host/port parsing)

## Suggested Fix

Add periodic reconnection attempts and better connection state management:

```cpp
// In AsyncHttpClient class:
void tryReconnect() {
    if (!m_connected) {
        checkHealth([this](const Response& resp) {
            if (resp.success) {
                m_connected = true;
                std::cout << "Backend reconnected\n";
            }
        });
    }
}

// Call periodically from main loop or before sending messages
```

Or simpler - always try the request regardless of `m_connected` state, let it fail naturally:

```cpp
// Remove isConnected() checks before sending messages
// Just send and handle failure in callback
```

## Test Commands

```bash
# Verify backend is running
curl -v http://localhost:8080/health

# Check for process
pgrep -af server.py

# Start backend
cd ~/Desktop/EDEN_Feb_2_2026_0550/backend
.venv/bin/python server.py

# Build and run terrain_editor
cd ~/Desktop/EDEN_Feb_2_2026_0550/build
make terrain_editor -j$(nproc)
./examples/terrain_editor/terrain_editor
```

## Expected Behavior
- terrain_editor should connect to backend on startup
- If backend starts after terrain_editor, connection should establish on next request
- Console should show "AI Backend connected successfully"
- In-game conversation should work with Xenk (AI_ARCHITECT)

## Priority
HIGH - Blocks all AI conversation functionality in the game
