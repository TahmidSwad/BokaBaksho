"""Allow ``python -m pc_client``."""

from pc_client.main import run

if __name__ == "__main__":
    raise SystemExit(run())
