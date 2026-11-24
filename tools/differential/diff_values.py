#!/usr/bin/env python3
"""Shared parsed-value helpers for differential replay generators."""
from __future__ import annotations

import json

CMP_STRING_TYPES = {'std::string'}
CMP_CHAR_TYPES = {'char', 'signed char', 'unsigned char'}
CMP_FLOAT_TYPES = {'float', 'double', 'long double'}
CMP_SIGNED_TYPES = {
    'int', 'short', 'long', 'long long',
    'std::int8_t', 'std::int16_t', 'std::int32_t', 'std::int64_t',
}
CMP_UNSIGNED_TYPES = {
    'unsigned', 'unsigned short', 'unsigned long', 'unsigned long long',
    'std::uint8_t', 'std::uint16_t', 'std::uint32_t', 'std::uint64_t',
}


def bool_expected(value: str) -> bool | None:
    low = value.lower()
    if low in ('true', 't', 'yes', 'y', 'on'):
        return True
    if low in ('false', 'f', 'no', 'n', 'off', '0'):
        return False
    try:
        return int(value, 0) > 0
    except ValueError:
        return None


def expected_scalar(cpp: str, value: str) -> str | None:
    """Return a C++ expression producing a canonical upstream scalar."""
    if cpp in CMP_STRING_TYPES:
        raise ValueError('string literals are emitted by the suite generator')
    if cpp == 'bool':
        parsed = bool_expected(value)
        return None if parsed is None else ('true' if parsed else 'false')
    if cpp in CMP_CHAR_TYPES:
        try:
            number = int(value, 0)
        except ValueError:
            return None
        return f'static_cast<{cpp}>({number})'
    if cpp in CMP_SIGNED_TYPES:
        return f'static_cast<{cpp}>(std::strtoll({json.dumps(value)}, nullptr, 0))'
    if cpp in CMP_UNSIGNED_TYPES:
        return f'static_cast<{cpp}>(std::strtoull({json.dumps(value)}, nullptr, 0))'
    if cpp in CMP_FLOAT_TYPES:
        return f'static_cast<{cpp}>(std::strtold({json.dumps(value)}, nullptr))'
    return None


def typed_scalar_text(value: dict) -> str | None:
    """Convert an additive parsed-values scalar object to canonical text."""
    kind = value.get('kind')
    if kind == 'string':
        return value.get('value')
    if kind == 'bool':
        raw = value.get('value')
        return 'true' if raw is True else 'false' if raw is False else None
    if kind in ('char', 'signed', 'unsigned', 'float'):
        raw = value.get('value')
        return None if raw is None else str(raw)
    return None


def typed_comparison(cpp: str, is_vector: bool, value: dict,
                     label: str, flag_lookup: str | None = None) -> dict | None:
    """Describe a typed value check, or None when shapes/types do not map."""
    if cpp in CMP_STRING_TYPES and not is_vector:
        if value.get('kind') != 'string':
            return None
        expected = value.get('value')
        if expected is None:
            return None
        # The caller replaces this marker with its byte-safe C++ literal.
        expected = ('string_literal', expected)
    else:
        expected = None

    if expected is None:
        if value.get('kind') == 'vector':
            if not is_vector:
                return None
            items = []
            for element in value.get('values', []):
                text = typed_scalar_text(element)
                if text is None:
                    return None
                if cpp in CMP_STRING_TYPES:
                    items.append(('string_literal', text))
                else:
                    literal = expected_scalar(cpp, text)
                    if literal is None:
                        return None
                    items.append(literal)
            expected = ('vector', items)
        else:
            if is_vector:
                return None
            text = typed_scalar_text(value)
            if text is None:
                return None
            expected = expected_scalar(cpp, text)
            if expected is None:
                return None

    if flag_lookup is not None:
        if cpp != 'bool' or is_vector or value.get('kind') != 'bool':
            return None
        return {
            'kind': 'flag',
            'lookup': flag_lookup,
            'name': label,
            'label': label,
            'expected': 'true' if value.get('value') is True else 'false',
        }

    return {
        'kind': 'value',
        'handle': None,
        'cpp': cpp,
        'vector': is_vector,
        'label': label,
        'expected': expected,
    }


CPP_VALUE_HELPERS = r'''
static bool diff_eq(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }
static bool diff_eq(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }
static bool diff_eq(long double a, long double b) { return a == b || (std::isnan(a) && std::isnan(b)); }
template <typename T>
static bool diff_eq(const T& a, const T& b) { return a == b; }
static bool diff_eq(const std::string& a, const char* b) { return a == b; }
static bool diff_eq(const char* a, const std::string& b) { return a == b; }
template <typename T>
static bool diff_eq(const std::vector<T>& a, const std::vector<T>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!diff_eq(a[i], b[i])) return false;
    }
    return true;
}
static std::string diff_repr(bool value) { return value ? "true" : "false"; }
static std::string diff_repr(char value) { return std::string(1, value); }
template <typename T>
static std::string diff_repr(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}
template <typename T>
static std::string diff_repr(const std::vector<T>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out += ", ";
        out += diff_repr(values[i]);
    }
    return out + "]";
}
'''
