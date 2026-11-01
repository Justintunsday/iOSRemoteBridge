"""Owner-authorized iOSRemoteBridge automation; Python 3.11+, no dependencies."""
from .client import BridgeClient, BridgeError
__all__ = ["BridgeClient", "BridgeError"]
