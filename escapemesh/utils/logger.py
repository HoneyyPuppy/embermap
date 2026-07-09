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
