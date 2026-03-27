import sys
import logging


class IndexLineHandler(logging.Handler):
    """
    A logging handler that overwrites specific terminal lines based on an index.
    Usage: logger.info(msg, extra={"line_index": idx})
    Each unique idx writes to the same terminal line.
    """

    def __init__(self, stream=None, base_offset=1):
        super().__init__()
        self.stream = stream or sys.stdout
        self.base_offset = base_offset
        self.max_index_seen = -1
        # Enable ANSI escape sequences on Windows PowerShell (usually enabled by default on modern builds)
        # This handler assumes VT sequences are supported.

    def emit(self, record: logging.LogRecord) -> None:
        try:
            msg = self.format(record)
            idx = getattr(record, "line_index", None)
            if idx is None:
                # If no index, just write as normal line below the managed area
                self.stream.write(msg + "\n")
                self.stream.flush()
                return

            if isinstance(idx, bool):
                idx = int(idx)
            elif not isinstance(idx, int):
                try:
                    idx = int(idx)
                except Exception:
                    idx = 0

            self.max_index_seen = max(self.max_index_seen, idx)

            # Save cursor position
            self.stream.write("\x1b7")
            # Move to target line (1-based). Column 1.
            target_line = self.base_offset + idx
            self.stream.write(f"\x1b[{target_line};1H")
            # Clear the line
            self.stream.write("\x1b[2K")
            # Write message (no trailing newline)
            self.stream.write(msg)
            # Restore cursor
            self.stream.write("\x1b8")
            self.stream.flush()
        except Exception:
            # Fallback to default behavior on any error
            try:
                self.stream.write(self.format(record) + "\n")
                self.stream.flush()
            except Exception:
                pass


def install_index_line_logging(base_offset: int = 1, level: int = logging.INFO) -> IndexLineHandler:
    """
    Installs the IndexLineHandler on the root logger and returns it.
    """
    handler = IndexLineHandler(base_offset=base_offset)
    formatter = logging.Formatter("%(asctime)s | %(name)s | %(levelname)s | %(message)s", datefmt="%Y-%m-%dT%H:%M:%SZ")
    handler.setFormatter(formatter)
    root = logging.getLogger()
    root.addHandler(handler)
    root.setLevel(level)
    return handler
