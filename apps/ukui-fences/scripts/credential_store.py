#!/usr/bin/env python3
"""Secret Service adapter. Secrets travel only through stdin/stdout pipes.

Requires the libsecret-1 runtime (no Python bindings or development SDK).
The caller owns timeouts and must retain legacy credentials on failure.
"""
import ctypes as c
import hashlib
import json
import sys


def main():
    operation, config_path = sys.argv[1:]
    profile = hashlib.sha256(config_path.encode()).hexdigest().encode()
    lib = c.CDLL("libsecret-1.so.0")
    glib = c.CDLL("libglib-2.0.so.0")
    lib.secret_schema_new.restype = c.c_void_p
    lib.secret_schema_new.argtypes = [c.c_char_p, c.c_int]
    lib.secret_schema_unref.argtypes = [c.c_void_p]
    lib.secret_password_free.argtypes = [c.c_void_p]
    glib.g_error_free.argtypes = [c.c_void_p]
    schema = lib.secret_schema_new(b"org.ukui.fences.SystemMonitor", 0,
                                  c.c_char_p(b"profile"), c.c_int(0), None)
    if not schema:
        raise RuntimeError("schema unavailable")
    error = c.c_void_p()
    attrs = (c.c_char_p(b"profile"), c.c_char_p(profile), None)
    try:
        if operation == "read":
            fn = lib.secret_password_lookup_sync
            fn.argtypes = [c.c_void_p, c.c_void_p, c.POINTER(c.c_void_p)]
            fn.restype = c.c_void_p
            value = fn(schema, None, c.byref(error), *attrs)
            if error.value:
                raise RuntimeError("lookup failed")
            try:
                print(json.dumps({"key": c.string_at(value).decode() if value else ""}))
            finally:
                if value:
                    lib.secret_password_free(value)
        elif operation == "write":
            key = json.load(sys.stdin)["key"]
            if not isinstance(key, str) or not key or "\x00" in key:
                raise ValueError("invalid key")
            fn = lib.secret_password_store_sync
            fn.argtypes = [c.c_void_p, c.c_char_p, c.c_char_p, c.c_char_p,
                           c.c_void_p, c.POINTER(c.c_void_p)]
            fn.restype = c.c_int
            if not fn(schema, b"default", b"UKUI Fences system monitor API key",
                      key.encode(), None, c.byref(error), *attrs):
                raise RuntimeError("store failed")
        elif operation == "clear":
            fn = lib.secret_password_clear_sync
            fn.argtypes = [c.c_void_p, c.c_void_p, c.POINTER(c.c_void_p)]
            fn.restype = c.c_int
            fn(schema, None, c.byref(error), *attrs)
            if error.value:
                raise RuntimeError("clear failed")
        else:
            raise ValueError("unknown operation")
    finally:
        if error.value:
            glib.g_error_free(error)
        lib.secret_schema_unref(schema)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        # Do not print exception payloads: they may contain secret input.
        print("System keyring unavailable or operation failed", file=sys.stderr)
        sys.exit(1)
