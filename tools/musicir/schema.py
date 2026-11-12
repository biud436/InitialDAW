"""music-ir.schema.json 으로 IR 을 검사한다. 스키마가 쓰는 draft 2020-12 의 일부만 본다
(type, const, enum, properties, required, additionalProperties, items, minItems, maxItems, minimum, maximum,
exclusiveMinimum, minLength, pattern, oneOf, $ref)."""
import json
import os
import re

SCHEMA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "music-ir.schema.json")
TYPES = {"object": dict, "array": list, "string": str, "boolean": bool, "null": type(None)}


def _is(v, t):
    if t == "integer":
        return isinstance(v, int) and not isinstance(v, bool) or isinstance(v, float) and v.is_integer()
    if t == "number":
        return isinstance(v, (int, float)) and not isinstance(v, bool)
    return isinstance(v, TYPES[t])


def errors(doc, schema=None):
    root = schema or json.load(open(SCHEMA))
    out = []

    def walk(v, s, path):
        if "$ref" in s:
            s = root["$defs"][s["$ref"].split("/")[-1]]
        if "type" in s:
            ts = s["type"] if isinstance(s["type"], list) else [s["type"]]
            if not any(_is(v, t) for t in ts):
                out.append(f"{path}: {type(v).__name__} 이지만 {'/'.join(ts)} 이어야 한다")
                return
        if "const" in s and v != s["const"]:
            out.append(f"{path}: {v!r} 이지만 {s['const']!r} 이어야 한다")
        if "enum" in s and v not in s["enum"]:
            out.append(f"{path}: {v!r} 은 {s['enum']} 중 하나여야 한다")
        if "oneOf" in s:
            hits = []
            for sub in s["oneOf"]:
                before = len(out)
                walk(v, sub, path)
                hits.append(len(out) == before)
                del out[before:]
            if sum(hits) != 1:
                out.append(f"{path}: oneOf 중 정확히 하나와 맞아야 한다")
        if isinstance(v, (int, float)) and not isinstance(v, bool):
            if "minimum" in s and v < s["minimum"]:
                out.append(f"{path}: {v} < {s['minimum']}")
            if "maximum" in s and v > s["maximum"]:
                out.append(f"{path}: {v} > {s['maximum']}")
            if "exclusiveMinimum" in s and v <= s["exclusiveMinimum"]:
                out.append(f"{path}: {v} <= {s['exclusiveMinimum']}")
        if isinstance(v, str):
            if len(v) < s.get("minLength", 0):
                out.append(f"{path}: 빈 문자열")
            if "pattern" in s and not re.search(s["pattern"], v):
                out.append(f"{path}: {v!r} 이 {s['pattern']} 와 맞지 않는다")
        if isinstance(v, dict):
            for k in s.get("required", []):
                if k not in v:
                    out.append(f"{path}: {k} 가 없다")
            props = s.get("properties", {})
            extra = s.get("additionalProperties", True)
            for k, x in v.items():
                if k in props:
                    walk(x, props[k], f"{path}.{k}")
                elif extra is False:
                    out.append(f"{path}: 모르는 키 {k}")
                elif isinstance(extra, dict):
                    walk(x, extra, f"{path}.{k}")
        if isinstance(v, list):
            if len(v) < s.get("minItems", 0):
                out.append(f"{path}: 항목이 {s['minItems']}개보다 적다")
            if "maxItems" in s and len(v) > s["maxItems"]:
                out.append(f"{path}: 항목이 {s['maxItems']}개보다 많다")
            if "items" in s:
                for i, x in enumerate(v):
                    walk(x, s["items"], f"{path}[{i}]")

    walk(doc, root, "$")
    return out
