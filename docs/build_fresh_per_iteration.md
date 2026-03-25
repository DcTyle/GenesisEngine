## Append-Only Logbook Pipeline Design

### Overview
The append-only logbook pipeline ensures that all logs are written sequentially without overwriting previous entries. This design is critical for maintaining a complete history of operations and debugging information.

### Key Components
1. **Log Writer**: A module responsible for appending new log entries to the logbook file.
2. **Timestamping**: Each log entry is prefixed with a timestamp to ensure chronological order.
3. **Concurrency Handling**: Mechanisms to prevent race conditions when multiple processes attempt to write to the logbook simultaneously.
4. **Log Rotation**: A strategy to archive old logs and prevent the logbook from growing indefinitely.

### Implementation Steps
1. **Initialize Logbook**:
   - Create a new logbook file if none exists.
   - Open the logbook in append mode.

2. **Write Log Entries**:
   - Format each entry with a timestamp and log level (INFO, WARNING, ERROR).
   - Append the entry to the logbook file.

3. **Handle Concurrency**:
   - Use file locks or a logging queue to serialize write operations.

4. **Implement Log Rotation**:
   - Define a maximum file size or age for the logbook.
   - Archive and compress old logs when the limit is reached.

### Example Code
```python
import os
import time
from threading import Lock

class Logbook:
    def __init__(self, filepath):
        self.filepath = filepath
        self.lock = Lock()

    def write_entry(self, level, message):
        timestamp = time.strftime('%Y-%m-%d %H:%M:%S')
        entry = f"[{timestamp}] [{level}] {message}\n"
        with self.lock:
            with open(self.filepath, 'a') as logbook:
                logbook.write(entry)

    def rotate_logs(self, max_size):
        if os.path.getsize(self.filepath) > max_size:
            archive_path = f"{self.filepath}.{int(time.time())}.gz"
            with open(self.filepath, 'rb') as logbook, open(archive_path, 'wb') as archive:
                archive.write(logbook.read())
            os.remove(self.filepath)

# Usage
logbook = Logbook('logbook.txt')
logbook.write_entry('INFO', 'Pipeline initialized.')
logbook.rotate_logs(10 * 1024 * 1024)  # Rotate if larger than 10MB
```

### Testing
- Verify that log entries are appended in the correct order.
- Simulate concurrent writes to ensure no data loss or corruption.
- Test log rotation with various file sizes and ages.
