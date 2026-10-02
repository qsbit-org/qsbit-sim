"""Live numerical adapters for the generic qsbit-sim backend contract."""

__all__ = ["AerBackend", "PulseBackend", "StimBackend"]


def __getattr__(name):
    if name == "AerBackend":
        from .aer import AerBackend
        return AerBackend
    if name == "PulseBackend":
        from .pulse import PulseBackend
        return PulseBackend
    if name == "StimBackend":
        from .stim import StimBackend
        return StimBackend
    raise AttributeError(name)
