import logging
import sys
import threading
import re
import shutil
from typing import Dict, List


class SingleLineStatusHandler(logging.Handler):
    """Logging handler that maintains one terminal line per key (network).

    - If stdout is a TTY and ANSI sequences are available, the handler will update
      an existing block of status lines in-place instead of printing new lines.
    - If not a TTY, the handler falls back to emitting normal log lines once.
    """

    COIN_RE = re.compile(r'coin=(\w+)')

    def __init__(self, keys_order: List[str] = None):
        super().__init__()
        self._lock = threading.Lock()
        self._statuses: Dict[str, str] = {}
        self._order: List[str] = []
        self._prev_count = 0
        self._isatty = sys.stdout.isatty()
        # reserve an initial ordering if provided
        if keys_order:
            for k in keys_order:
                self._order.append(k)

    def emit(self, record: logging.LogRecord) -> None:
        try:
            msg = self.format(record)
            # derive a network key: prefer 'coin=XYZ' in message, else last logger segment
            m = self.COIN_RE.search(record.getMessage())
            if m:
                key = m.group(1).upper()
            else:
                # use last name segment as fallback (e.g. 'kas' from 'miner.stratum_client.kas')
                try:
                    key = record.name.split('.')[-1]
                except Exception:
                    key = record.name
                key = key.upper()

            short = self._summarize(record, msg)

            with self._lock:
                if key not in self._statuses:
                    self._order.append(key)
                self._statuses[key] = short

                if not self._isatty:
                    # fallback: write one normal line per update (minimal noise fallback)
                    sys.stdout.write(short + "\n")
                    sys.stdout.flush()
                    return

                self._refresh_terminal()

        except Exception:
            self.handleError(record)

    def _summarize(self, record: logging.LogRecord, formatted: str) -> str:
        # Keep compact summary: TIME | LOGGER | LEVEL | message (trimmed to terminal width)
        try:
            t = ''
            if hasattr(record, 'asctime'):
                t = record.asctime + ' | '
        except Exception:
            t = ''
        summary = f"{t}{record.name} | {record.levelname} | {record.getMessage()}"
        # truncate to terminal width
        try:
            width = shutil.get_terminal_size((120, 20)).columns
            if len(summary) > width - 1:
                summary = summary[: max(0, width - 4)] + '...'
        except Exception:
            pass
        return summary

    def _refresh_terminal(self):
        # Move cursor up to the start of the status block, clear and rewrite all lines
        lines = [self._statuses[k] for k in self._order]

        # If there were previously printed lines, move cursor up that many
        if self._prev_count:
            # ANSI: move cursor up N lines
            sys.stdout.write(f"\x1b[{self._prev_count}A")

        for line in lines:
            # Clear the full line and write new content
            sys.stdout.write("\x1b[2K")
            sys.stdout.write(line + "\n")

        sys.stdout.flush()
        self._prev_count = len(lines)
