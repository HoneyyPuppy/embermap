import sys
import os
import re

class ColorLogger:
    @staticmethod
    def info(msg: str):
        print(f"\033[92m[INFO] {msg}\033[0m")

    @staticmethod
    def warn(msg: str):
        print(f"\033[93m[WARN] {msg}\033[0m")

    @staticmethod
    def error(msg: str):
        print(f"\033[91m[ERROR] {msg}\033[0m")

class Tee:
    """Redirects stdout to both the console and a file, stripping ANSI color codes for the file log."""
    def __init__(self, filename: str):
        self.terminal = sys.stdout
        log_dir = os.path.dirname(filename)
        if log_dir:
            os.makedirs(log_dir, exist_ok=True)
        self.log = open(filename, "w", encoding="utf-8")
        
    def write(self, message):
        self.terminal.write(message)
        ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
        clean_message = ansi_escape.sub('', message)
        self.log.write(clean_message)
        self.log.flush()
        
    def flush(self):
        self.terminal.flush()
        self.log.flush()
