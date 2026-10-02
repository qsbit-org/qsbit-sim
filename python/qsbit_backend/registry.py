"""Discover adapters and validate backend-owned configuration."""

from copy import deepcopy
from importlib import import_module, metadata
import json
import math
from urllib.parse import quote

from jsonschema import Draft202012Validator
from jsonschema.exceptions import best_match

from .catalog import builtins, obj


def _load(target):
    if target.count(":") != 1 or not all(target.split(":")):
        raise ValueError("backend factory must be MODULE:CLASS")
    module, name = target.split(":")
    return getattr(import_module(module), name)


def descriptions():
    result = builtins()
    for entry in metadata.entry_points(group="qsbit_sim.backends"):
        if entry.name in result or entry.name == "mock":
            raise ValueError(f"duplicate backend name: {entry.name}")
        result[entry.name] = entry.load()()
    return result


def describe(name):
    if name == "mock":
        return {"api_version": 1, "options_schema": obj({}), "requirements": [],
                "capabilities": {"state_outputs": [], "measurement": "configured outcomes"}}
    catalog = descriptions()
    if name in catalog:
        result = deepcopy(catalog[name])
    elif any(item["factory"] == name for item in catalog.values()):
        result = deepcopy(next(item for item in catalog.values() if item["factory"] == name))
    elif ":" in name:
        cls = _load(name)
        describe_method = getattr(cls, "describe", None)
        result = describe_method() if describe_method else {
            "api_version": 1, "options_schema": obj({}), "requirements": [],
            "legacy": True, "capabilities": {"description": "Adapter-defined operations."}}
        result = dict(result, factory=name)
    else:
        raise ValueError(f"unknown backend {name!r}; use --list-backends")
    if result.get("api_version") != 1:
        raise ValueError(f"unsupported descriptor API for {name}")
    Draft202012Validator.check_schema(result["options_schema"])
    return result


def _finite(value, path="backend_options"):
    if isinstance(value, float) and not math.isfinite(value):
        raise ValueError(f"{path}: must be finite")
    if isinstance(value, dict):
        for key, child in value.items():
            _finite(child, f"{path}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            _finite(child, f"{path}[{index}]")


def options(name, value):
    schema = describe(name)["options_schema"]
    _finite(value)
    error = best_match(Draft202012Validator(schema).iter_errors(value))
    if error:
        raise ValueError(f"backend_options{_path(error.absolute_path)}: {error.message}")
    result = deepcopy(value)
    _defaults(result, schema)
    _finite(result)
    error = best_match(Draft202012Validator(schema).iter_errors(result))
    if error:
        raise ValueError(f"backend_options{_path(error.absolute_path)}: {error.message}")
    return result


def _path(parts):
    return "".join(f"[{p}]" if isinstance(p, int) else f".{p}" for p in parts)


def _defaults(value, schema):
    if isinstance(value, dict):
        for key, child in schema.get("properties", {}).items():
            if key not in value and "default" in child:
                value[key] = deepcopy(child["default"])
            if key in value:
                _defaults(value[key], child)
        for branch in schema.get("oneOf", []):
            if Draft202012Validator(branch).is_valid(value):
                _defaults(value, branch)
                break
    elif isinstance(value, list):
        for item in value:
            _defaults(item, schema.get("items", {}))


def missing(desc):
    absent = []
    for package in desc.get("requirements", []):
        try:
            metadata.version(package)
        except metadata.PackageNotFoundError:
            absent.append(package)
    return absent


def create(name, value):
    desc = describe(name)
    resolved = options(name, value)
    absent = missing(desc)
    if absent:
        raise ValueError(f"{name}: missing {', '.join(absent)}; {desc.get('install', 'install adapter dependencies')}")
    cls = _load(desc["factory"])
    backend = cls() if desc.get("legacy") else cls(resolved)
    return backend, resolved


def inspect_backend(name, command):
    if command == "list":
        return json.dumps([{ "name": key, "missing_dependencies": missing(describe(key)),
                             "install": describe(key).get("install", "included")}
                           for key in ["mock", *sorted(descriptions())]], indent=2)
    desc = describe(name)
    if command == "help":
        return json.dumps(dict(desc, name=name, missing_dependencies=missing(desc)), indent=2)
    if command == "schema":
        names = list(dict.fromkeys(["mock", *sorted(descriptions()), name]))
        return json.dumps({"$schema": "https://json-schema.org/draft/2020-12/schema",
            "type": "object", "properties": {"backend": {"enum": names}},
            "required": ["backend"], "allOf": [
                {"if": {"properties": {"backend": {"const": key}}},
                 "then": {"properties": {"backend_options": _embedded_schema(key)}}}
                for key in names]}, indent=2)
    if command == "generate":
        return json.dumps({"schema": 1, "backend": name,
                           "backend_options": template(desc["options_schema"]),
                           "program": "program.elf"}, indent=2)
    raise ValueError(f"unknown backend command: {command}")


def _embedded_schema(name):
    schema = deepcopy(describe(name)["options_schema"])
    schema.setdefault("$id", "urn:qsbit-sim:backend-options:" + quote(name, safe=""))
    return schema


def template(schema):
    if "default" in schema:
        return deepcopy(schema["default"])
    if "const" in schema:
        return deepcopy(schema["const"])
    if schema.get("type") == "object":
        return {key: template(child) for key, child in schema.get("properties", {}).items()
                if key in schema.get("required", []) or "default" in child}
    return None
