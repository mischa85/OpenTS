#!/usr/bin/env python3
"""Compiles the language resource script into a resource directory.

The Visual Studio target builds language.rc into a resource-only DLL and asks the
operating system for the strings and dialog templates in it. Every other target has
no module loader to ask, so the same script is compiled here into the directory a
resource section holds, which the engine carries with it and reads in place.

The subset understood is the one language.rc uses: object-like macros and the
conditionals around them, STRINGTABLE, DIALOG, DIALOGEX and VERSIONINFO. A DLGINIT
block is read and dropped, because it belongs to the MFC controls the engine does not
create. Any other statement is refused rather than quietly lost.
"""

import argparse
import os
import re
import struct
import sys

# ---------------------------------------------------------------------------
# Win32 resource constants
# ---------------------------------------------------------------------------
WIN32_DEFINES = {
    # Window styles
    "WS_OVERLAPPED": 0x00000000, "WS_POPUP": 0x80000000, "WS_CHILD": 0x40000000,
    "WS_MINIMIZE": 0x20000000, "WS_VISIBLE": 0x10000000, "WS_DISABLED": 0x08000000,
    "WS_CLIPSIBLINGS": 0x04000000, "WS_CLIPCHILDREN": 0x02000000, "WS_MAXIMIZE": 0x01000000,
    "WS_CAPTION": 0x00C00000, "WS_BORDER": 0x00800000, "WS_DLGFRAME": 0x00400000,
    "WS_VSCROLL": 0x00200000, "WS_HSCROLL": 0x00100000, "WS_SYSMENU": 0x00080000,
    "WS_THICKFRAME": 0x00040000, "WS_GROUP": 0x00020000, "WS_TABSTOP": 0x00010000,
    "WS_MINIMIZEBOX": 0x00020000, "WS_MAXIMIZEBOX": 0x00010000,
    # Extended window styles
    "WS_EX_DLGMODALFRAME": 0x00000001, "WS_EX_NOPARENTNOTIFY": 0x00000004,
    "WS_EX_TOPMOST": 0x00000008, "WS_EX_ACCEPTFILES": 0x00000010,
    "WS_EX_TRANSPARENT": 0x00000020, "WS_EX_MDICHILD": 0x00000040,
    "WS_EX_TOOLWINDOW": 0x00000080, "WS_EX_WINDOWEDGE": 0x00000100,
    "WS_EX_CLIENTEDGE": 0x00000200, "WS_EX_CONTEXTHELP": 0x00000400,
    "WS_EX_RIGHT": 0x00001000, "WS_EX_LEFT": 0x00000000, "WS_EX_RTLREADING": 0x00002000,
    "WS_EX_LEFTSCROLLBAR": 0x00004000, "WS_EX_CONTROLPARENT": 0x00010000,
    "WS_EX_STATICEDGE": 0x00020000, "WS_EX_APPWINDOW": 0x00040000,
    # Dialog styles
    "DS_ABSALIGN": 0x0001, "DS_SYSMODAL": 0x0002, "DS_LOCALEDIT": 0x0020,
    "DS_SETFONT": 0x0040, "DS_MODALFRAME": 0x0080, "DS_NOIDLEMSG": 0x0100,
    "DS_SETFOREGROUND": 0x0200, "DS_3DLOOK": 0x0004, "DS_FIXEDSYS": 0x0008,
    "DS_NOFAILCREATE": 0x0010, "DS_CONTROL": 0x0400, "DS_CENTER": 0x0800,
    "DS_CENTERMOUSE": 0x1000, "DS_CONTEXTHELP": 0x2000, "DS_SHELLFONT": 0x0048,
    # Button styles
    "BS_PUSHBUTTON": 0x0000, "BS_DEFPUSHBUTTON": 0x0001, "BS_CHECKBOX": 0x0002,
    "BS_AUTOCHECKBOX": 0x0003, "BS_RADIOBUTTON": 0x0004, "BS_3STATE": 0x0005,
    "BS_AUTO3STATE": 0x0006, "BS_GROUPBOX": 0x0007, "BS_USERBUTTON": 0x0008,
    "BS_AUTORADIOBUTTON": 0x0009, "BS_OWNERDRAW": 0x000B, "BS_LEFTTEXT": 0x0020,
    "BS_TEXT": 0x0000, "BS_ICON": 0x0040, "BS_BITMAP": 0x0080, "BS_LEFT": 0x0100,
    "BS_RIGHT": 0x0200, "BS_CENTER": 0x0300, "BS_TOP": 0x0400, "BS_BOTTOM": 0x0800,
    "BS_VCENTER": 0x0C00, "BS_PUSHLIKE": 0x1000, "BS_MULTILINE": 0x2000,
    "BS_NOTIFY": 0x4000, "BS_FLAT": 0x8000,
    # Static styles
    "SS_LEFT": 0x0000, "SS_CENTER": 0x0001, "SS_RIGHT": 0x0002, "SS_ICON": 0x0003,
    "SS_BLACKRECT": 0x0004, "SS_GRAYRECT": 0x0005, "SS_WHITERECT": 0x0006,
    "SS_BLACKFRAME": 0x0007, "SS_GRAYFRAME": 0x0008, "SS_WHITEFRAME": 0x0009,
    "SS_USERITEM": 0x000A, "SS_SIMPLE": 0x000B, "SS_LEFTNOWORDWRAP": 0x000C,
    "SS_OWNERDRAW": 0x000D, "SS_BITMAP": 0x000E, "SS_ENHMETAFILE": 0x000F,
    "SS_ETCHEDHORZ": 0x0010, "SS_ETCHEDVERT": 0x0011, "SS_ETCHEDFRAME": 0x0012,
    "SS_NOPREFIX": 0x0080, "SS_NOTIFY": 0x0100, "SS_CENTERIMAGE": 0x0200,
    "SS_RIGHTJUST": 0x0400, "SS_REALSIZEIMAGE": 0x0800, "SS_SUNKEN": 0x1000,
    # Edit styles
    "ES_LEFT": 0x0000, "ES_CENTER": 0x0001, "ES_RIGHT": 0x0002, "ES_MULTILINE": 0x0004,
    "ES_UPPERCASE": 0x0008, "ES_LOWERCASE": 0x0010, "ES_PASSWORD": 0x0020,
    "ES_AUTOVSCROLL": 0x0040, "ES_AUTOHSCROLL": 0x0080, "ES_NOHIDESEL": 0x0100,
    "ES_OEMCONVERT": 0x0400, "ES_READONLY": 0x0800, "ES_WANTRETURN": 0x1000,
    "ES_NUMBER": 0x2000,
    # List box styles
    "LBS_NOTIFY": 0x0001, "LBS_SORT": 0x0002, "LBS_NOREDRAW": 0x0004,
    "LBS_MULTIPLESEL": 0x0008, "LBS_OWNERDRAWFIXED": 0x0010,
    "LBS_OWNERDRAWVARIABLE": 0x0020, "LBS_HASSTRINGS": 0x0040,
    "LBS_USETABSTOPS": 0x0080, "LBS_NOINTEGRALHEIGHT": 0x0100,
    "LBS_MULTICOLUMN": 0x0200, "LBS_WANTKEYBOARDINPUT": 0x0400,
    "LBS_EXTENDEDSEL": 0x0800, "LBS_DISABLENOSCROLL": 0x1000, "LBS_NODATA": 0x2000,
    "LBS_NOSEL": 0x4000,
    # Combo box styles
    "CBS_SIMPLE": 0x0001, "CBS_DROPDOWN": 0x0002, "CBS_DROPDOWNLIST": 0x0003,
    "CBS_OWNERDRAWFIXED": 0x0010, "CBS_OWNERDRAWVARIABLE": 0x0020,
    "CBS_AUTOHSCROLL": 0x0040, "CBS_OEMCONVERT": 0x0080, "CBS_SORT": 0x0100,
    "CBS_HASSTRINGS": 0x0200, "CBS_NOINTEGRALHEIGHT": 0x0400,
    "CBS_DISABLENOSCROLL": 0x0800, "CBS_UPPERCASE": 0x2000, "CBS_LOWERCASE": 0x4000,
    # Scroll bar styles
    "SBS_HORZ": 0x0000, "SBS_VERT": 0x0001,
    # Common control styles the script names
    "TBS_AUTOTICKS": 0x0001, "TBS_VERT": 0x0002, "TBS_HORZ": 0x0000,
    "TBS_TOP": 0x0004, "TBS_BOTTOM": 0x0000, "TBS_LEFT": 0x0004,
    "TBS_RIGHT": 0x0000, "TBS_BOTH": 0x0008, "TBS_NOTICKS": 0x0010,
    "TBS_ENABLESELRANGE": 0x0020, "TBS_FIXEDLENGTH": 0x0040, "TBS_NOTHUMB": 0x0080,
    "LVS_ICON": 0x0000, "LVS_REPORT": 0x0001, "LVS_SMALLICON": 0x0002,
    "LVS_LIST": 0x0003, "LVS_SINGLESEL": 0x0004, "LVS_SHOWSELALWAYS": 0x0008,
    "LVS_SORTASCENDING": 0x0010, "LVS_SORTDESCENDING": 0x0020,
    "LVS_SHAREIMAGELISTS": 0x0040, "LVS_NOLABELWRAP": 0x0080,
    "LVS_AUTOARRANGE": 0x0100, "LVS_EDITLABELS": 0x0200,
    # Standard control identifiers
    "IDOK": 1, "IDCANCEL": 2, "IDABORT": 3, "IDRETRY": 4, "IDIGNORE": 5,
    "IDYES": 6, "IDNO": 7, "IDCLOSE": 8, "IDHELP": 9,
    # Version resource
    "VS_VERSION_INFO": 1, "VS_FF_DEBUG": 0x00000001, "VS_FF_PRERELEASE": 0x00000002,
    "VS_FF_PATCHED": 0x00000004, "VS_FF_PRIVATEBUILD": 0x00000008,
    "VS_FF_INFOINFERRED": 0x00000010, "VS_FF_SPECIALBUILD": 0x00000020,
    "VOS_UNKNOWN": 0x00000000, "VOS__WINDOWS32": 0x00000004,
    "VFT_UNKNOWN": 0x00000000, "VFT_APP": 0x00000001, "VFT_DLL": 0x00000002,
    "VFT2_UNKNOWN": 0x00000000,
    # Language identifiers
    "LANG_ENGLISH": 0x09, "SUBLANG_ENGLISH_US": 0x01, "SUBLANG_NEUTRAL": 0x00,
}

RESOURCE_TYPE_DIALOG = 5
RESOURCE_TYPE_STRING = 6
RESOURCE_TYPE_VERSION = 16

STOCK_CLASSES = {
    "button": 0x0080, "edit": 0x0081, "static": 0x0082,
    "listbox": 0x0083, "scrollbar": 0x0084, "combobox": 0x0085,
}


class Error(Exception):
    pass


# ---------------------------------------------------------------------------
# Lexing
# ---------------------------------------------------------------------------
TOKEN_PATTERN = re.compile(r"""
      (?P<space>[ \t\f\v]+)
    | (?P<newline>\r?\n)
    | (?P<comment>//[^\n]*|/\*.*?\*/)
    | (?P<string>"(?:[^"\\]|\\.|"")*")
    | (?P<number>0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*)
    | (?P<ident>[A-Za-z_][A-Za-z_0-9]*)
    | (?P<punct>\|\||&&|<<|>>|==|!=|<=|>=|[()|,&~+\-*/!<>=^])
""", re.X | re.S)


class Token:
    __slots__ = ("kind", "text", "line")

    def __init__(self, kind, text, line):
        self.kind = kind
        self.text = text
        self.line = line

    def __repr__(self):
        return "Token(%s,%r,%d)" % (self.kind, self.text, self.line)


def tokenize(text, line=1):
    tokens = []
    position = 0
    length = len(text)

    while position < length:
        match = TOKEN_PATTERN.match(text, position)
        if match is None:
            raise Error("line %d: cannot read %r" % (line, text[position:position + 20]))

        position = match.end()
        kind = match.lastgroup
        body = match.group()

        if kind == "newline":
            tokens.append(Token("newline", "\n", line))
            line += 1
        elif kind == "comment":
            line += body.count("\n")
            if "\n" in body:
                tokens.append(Token("newline", "\n", line))
        elif kind == "space":
            pass
        else:
            tokens.append(Token(kind, body, line))

    return tokens


def unescape(literal):
    """Turns one resource string literal into its characters."""
    body = literal[1:-1]
    out = []
    index = 0

    while index < len(body):
        character = body[index]

        if character == '"' and index + 1 < len(body) and body[index + 1] == '"':
            out.append('"')
            index += 2
            continue

        if character != "\\":
            out.append(character)
            index += 1
            continue

        index += 1
        if index >= len(body):
            break

        escape = body[index]
        index += 1

        if escape == "a":
            out.append("\a")
        elif escape == "b":
            out.append("\b")
        elif escape == "f":
            out.append("\f")
        elif escape == "n":
            out.append("\n")
        elif escape == "r":
            out.append("\r")
        elif escape == "t":
            out.append("\t")
        elif escape == "v":
            out.append("\v")
        elif escape == "\\":
            out.append("\\")
        elif escape == '"':
            out.append('"')
        elif escape == "x":
            digits = ""
            while index < len(body) and len(digits) < 4 and body[index] in "0123456789abcdefABCDEF":
                digits += body[index]
                index += 1
            out.append(chr(int(digits, 16) & 0xFFFF) if digits else "x")
        elif escape in "01234567":
            digits = escape
            while index < len(body) and len(digits) < 3 and body[index] in "01234567":
                digits += body[index]
                index += 1
            out.append(chr(int(digits, 8) & 0xFF))
        else:
            out.append(escape)

    return "".join(out)


# ---------------------------------------------------------------------------
# Preprocessing
# ---------------------------------------------------------------------------
class Preprocessor:
    """Expands the directives the language script uses, and nothing more."""

    def __init__(self, includes, defines):
        self.includes = list(includes)
        self.defines = dict(defines)
        self.output = []

    def find_include(self, name, directory):
        candidate = os.path.join(directory, name)
        if os.path.isfile(candidate):
            return candidate

        for include in self.includes:
            candidate = os.path.join(include, name)
            if os.path.isfile(candidate):
                return candidate

        return None

    def read_source(self, path):
        with open(path, "rb") as handle:
            return handle.read().decode("cp1252")

    def process(self, path):
        source = self.read_source(path)
        directory = os.path.dirname(os.path.abspath(path))
        lines = source.split("\n")
        # A directive is recognized on a physical line; everything else is tokenized
        # as a run so that a statement may still be spread over several lines.
        states = [True]
        seen = [False]
        block = []
        number = 0

        def flush():
            if block:
                self.output.extend(tokenize("\n".join(block) + "\n", number - len(block) + 1))
                del block[:]

        for line in lines:
            number += 1
            stripped = line.strip()
            live = all(states)

            if not stripped.startswith("#"):
                if live:
                    block.append(line)
                continue

            flush()
            directive = stripped[1:].strip()
            word = re.match(r"(\w+)\s*(.*)$", directive, re.S)
            if word is None:
                continue

            name = word.group(1)
            rest = word.group(2).strip()

            if name == "ifdef":
                states.append(live and rest.split()[0] in self.defines)
                seen.append(states[-1])
            elif name == "ifndef":
                states.append(live and rest.split()[0] not in self.defines)
                seen.append(states[-1])
            elif name == "if":
                states.append(live and self.evaluate(rest) != 0)
                seen.append(states[-1])
            elif name == "elif":
                outer = all(states[:-1])
                states[-1] = outer and not seen[-1] and self.evaluate(rest) != 0
                seen[-1] = seen[-1] or states[-1]
            elif name == "else":
                outer = all(states[:-1])
                states[-1] = outer and not seen[-1]
                seen[-1] = True
            elif name == "endif":
                if len(states) == 1:
                    raise Error("line %d: #endif without a condition" % number)
                states.pop()
                seen.pop()
            elif not live:
                continue
            elif name == "define":
                self.define(rest)
            elif name == "undef":
                self.defines.pop(rest.split()[0], None)
            elif name == "include":
                self.include(rest, directory, number)
            elif name in ("pragma", "line"):
                pass
            elif name == "error":
                raise Error("line %d: %s" % (number, rest))
            else:
                raise Error("line %d: unsupported directive #%s" % (number, name))

        flush()

        if len(states) != 1:
            raise Error("%s ends inside a conditional" % path)

    def define(self, rest):
        match = re.match(r"(\w+)\s*(.*)$", rest, re.S)
        if match is None:
            return

        name = match.group(1)
        body = match.group(2)
        body = re.sub(r"//.*$", "", body)
        body = re.sub(r"/\*.*?\*/", " ", body, flags=re.S)
        self.defines[name] = tokenize(body.strip()) if body.strip() else []

    def include(self, rest, directory, number):
        match = re.match(r'[<"]([^>"]+)[>"]', rest)
        if match is None:
            raise Error("line %d: cannot read the #include" % number)

        found = self.find_include(match.group(1), directory)
        if found is None:
            raise Error("line %d: cannot find %s" % (number, match.group(1)))

        self.process(found)

    def evaluate(self, text):
        tokens = [t for t in tokenize(text) if t.kind != "newline"]
        replaced = []
        index = 0

        while index < len(tokens):
            token = tokens[index]

            if token.kind == "ident" and token.text == "defined":
                index += 1
                if index < len(tokens) and tokens[index].text == "(":
                    index += 1
                    name = tokens[index].text
                    index += 2
                else:
                    name = tokens[index].text
                    index += 1
                replaced.append(Token("number", "1" if name in self.defines else "0", token.line))
                continue

            replaced.append(token)
            index += 1

        return evaluate_expression(expand(replaced, self.defines))


def expand(tokens, defines, depth=0):
    """Replaces defined names by their bodies, as many rounds as it takes."""
    if depth > 32:
        raise Error("a macro expands into itself")

    out = []
    changed = False

    for token in tokens:
        if token.kind == "ident" and token.text in defines:
            out.extend(defines[token.text])
            changed = True
        else:
            out.append(token)

    return expand(out, defines, depth + 1) if changed else out


# ---------------------------------------------------------------------------
# Expressions
# ---------------------------------------------------------------------------
class Stream:
    def __init__(self, tokens):
        self.tokens = tokens
        self.point = 0

    def peek(self, skipnewlines=True):
        point = self.point
        while skipnewlines and point < len(self.tokens) and self.tokens[point].kind == "newline":
            point += 1
        return self.tokens[point] if point < len(self.tokens) else None

    def next_token(self, skipnewlines=True):
        while skipnewlines and self.point < len(self.tokens) and self.tokens[self.point].kind == "newline":
            self.point += 1
        if self.point >= len(self.tokens):
            raise Error("the script ends in the middle of a statement")
        token = self.tokens[self.point]
        self.point += 1
        return token

    def accept(self, text):
        token = self.peek()
        if token is not None and token.text.upper() == text.upper():
            self.next_token()
            return True
        return False

    def expect(self, text):
        token = self.next_token()
        if token.text.upper() != text.upper():
            raise Error("line %d: expected %s, found %r" % (token.line, text, token.text))
        return token


def evaluate_expression(tokens):
    return parse_or(Stream(tokens))


def parse_or(stream):
    value = parse_and(stream)

    while True:
        token = stream.peek()
        if token is None:
            break

        if token.text == "|":
            stream.next_token()
            if stream.peek() is not None and stream.peek().text.upper() == "NOT":
                stream.next_token()
                value &= ~parse_and(stream) & 0xFFFFFFFF
            else:
                value |= parse_and(stream)
        elif token.text.upper() == "NOT":
            stream.next_token()
            value &= ~parse_and(stream) & 0xFFFFFFFF
        elif token.text == "||":
            stream.next_token()
            value = 1 if (value or parse_and(stream)) else 0
        else:
            break

    return value & 0xFFFFFFFF


def parse_and(stream):
    value = parse_additive(stream)

    while stream.peek() is not None and stream.peek().text in ("&", "&&"):
        operator = stream.next_token().text
        right = parse_additive(stream)
        value = (value & right) if operator == "&" else (1 if (value and right) else 0)

    return value


def parse_additive(stream):
    value = parse_unary(stream)

    while stream.peek() is not None and stream.peek().text in ("+", "-"):
        operator = stream.next_token().text
        right = parse_unary(stream)
        value = value + right if operator == "+" else value - right

    return value & 0xFFFFFFFF


def parse_unary(stream):
    token = stream.peek()

    if token is not None and token.text == "-":
        stream.next_token()
        return (-parse_unary(stream)) & 0xFFFFFFFF

    if token is not None and token.text == "~":
        stream.next_token()
        return (~parse_unary(stream)) & 0xFFFFFFFF

    if token is not None and token.text == "!":
        stream.next_token()
        return 0 if parse_unary(stream) else 1

    if token is not None and token.text.upper() == "NOT":
        stream.next_token()
        return (~parse_unary(stream)) & 0xFFFFFFFF

    return parse_primary(stream)


def parse_primary(stream):
    token = stream.next_token()

    if token.text == "(":
        value = parse_or(stream)
        stream.expect(")")
        return value

    if token.kind == "number":
        return int(token.text.rstrip("uUlL"), 0) & 0xFFFFFFFF

    raise Error("line %d: %r is not a number" % (token.line, token.text))


def parse_style(stream, initial):
    """Reads a style expression, which starts from the statement's own default."""
    value = initial

    while True:
        token = stream.peek()
        if token is not None and token.text.upper() == "NOT":
            stream.next_token()
            value &= ~parse_and(stream) & 0xFFFFFFFF
        else:
            value |= parse_and(stream)

        token = stream.peek()
        if token is not None and token.text == "|":
            stream.next_token()
            continue
        break

    return value & 0xFFFFFFFF


def parse_number(stream):
    return parse_or(stream)


def parse_string(stream):
    """Reads one string, joining the literals a long one is written as."""
    text = ""
    token = stream.peek()

    while token is not None and token.kind == "string":
        text += unescape(stream.next_token().text)
        token = stream.peek()

    return text


# ---------------------------------------------------------------------------
# Dialog templates
# ---------------------------------------------------------------------------
CONTROL_DEFAULTS = {
    "LTEXT":            (0x50020000, "static",    0x0000),
    "RTEXT":            (0x50020000, "static",    0x0002),
    "CTEXT":            (0x50020000, "static",    0x0001),
    "ICON":             (0x50000000, "static",    0x0003),
    "PUSHBUTTON":       (0x50010000, "button",    0x0000),
    "DEFPUSHBUTTON":    (0x50010000, "button",    0x0001),
    "CHECKBOX":         (0x50010000, "button",    0x0002),
    "AUTOCHECKBOX":     (0x50010000, "button",    0x0003),
    "RADIOBUTTON":      (0x50000000, "button",    0x0004),
    "STATE3":           (0x50010000, "button",    0x0005),
    "AUTO3STATE":       (0x50010000, "button",    0x0006),
    "AUTORADIOBUTTON":  (0x50000000, "button",    0x0009),
    "GROUPBOX":         (0x50000000, "button",    0x0007),
    "EDITTEXT":         (0x50810000, "edit",      0x0000),
    "COMBOBOX":         (0x50000000, "combobox",  0x0000),
    "LISTBOX":          (0x50800000, "listbox",   0x0001),
    "SCROLLBAR":        (0x50000000, "scrollbar", 0x0000),
}

TEXTLESS_CONTROLS = ("EDITTEXT", "COMBOBOX", "LISTBOX", "SCROLLBAR")


class DialogItem:
    def __init__(self):
        self.help_id = 0
        self.style = 0
        self.ex_style = 0
        self.x = 0
        self.y = 0
        self.cx = 0
        self.cy = 0
        self.identifier = 0
        self.class_name = None
        self.text = ""


class Dialog:
    def __init__(self, name, extended):
        self.name = name
        self.extended = extended
        self.help_id = 0
        self.style = None
        self.ex_style = 0
        self.x = self.y = self.cx = self.cy = 0
        self.caption = ""
        self.class_name = None
        self.menu = None
        self.font = None
        self.items = []


def parse_dialog(stream, name, extended):
    dialog = Dialog(name, extended)

    while stream.accept("DISCARDABLE") or stream.accept("LOADONCALL") or stream.accept("MOVEABLE") \
            or stream.accept("PURE") or stream.accept("IMPURE") or stream.accept("FIXED") \
            or stream.accept("PRELOAD"):
        pass

    dialog.x = parse_number(stream)
    stream.expect(",")
    dialog.y = parse_number(stream)
    stream.expect(",")
    dialog.cx = parse_number(stream)
    stream.expect(",")
    dialog.cy = parse_number(stream)

    if stream.peek() is not None and stream.peek().text == ",":
        stream.next_token()
        dialog.help_id = parse_number(stream)

    while True:
        token = stream.peek()
        if token is None:
            raise Error("a dialog runs off the end of the script")

        word = token.text.upper()

        if word == "BEGIN":
            stream.next_token()
            break
        if word == "STYLE":
            stream.next_token()
            dialog.style = parse_style(stream, 0)
        elif word == "EXSTYLE":
            stream.next_token()
            dialog.ex_style = parse_style(stream, 0)
        elif word == "CAPTION":
            stream.next_token()
            dialog.caption = parse_string(stream)
        elif word == "CLASS":
            stream.next_token()
            dialog.class_name = parse_string(stream) if stream.peek().kind == "string" else parse_number(stream)
        elif word == "MENU":
            stream.next_token()
            dialog.menu = parse_string(stream) if stream.peek().kind == "string" else parse_number(stream)
        elif word == "FONT":
            stream.next_token()
            size = parse_number(stream)
            stream.expect(",")
            face = parse_string(stream)
            weight = italic = charset = 0
            if stream.peek() is not None and stream.peek().text == ",":
                stream.next_token()
                weight = parse_number(stream)
                stream.expect(",")
                italic = parse_number(stream)
                stream.expect(",")
                charset = parse_number(stream)
            dialog.font = (size, face, weight, italic, charset)
        elif word in ("CHARACTERISTICS", "VERSION"):
            stream.next_token()
            parse_number(stream)
        elif word == "LANGUAGE":
            stream.next_token()
            parse_number(stream)
            stream.expect(",")
            parse_number(stream)
        else:
            raise Error("line %d: unexpected %r in a dialog" % (token.line, token.text))

    while not stream.accept("END"):
        dialog.items.append(parse_dialog_item(stream, dialog.extended))

    if dialog.style is None:
        dialog.style = 0x80000000 | 0x00800000 | 0x00080000
    if dialog.font is not None:
        dialog.style |= WIN32_DEFINES["DS_SETFONT"]

    return dialog


def parse_dialog_item(stream, extended):
    token = stream.next_token()
    word = token.text.upper()
    item = DialogItem()

    if word == "CONTROL":
        item.text = parse_string(stream) if stream.peek().kind == "string" else parse_number(stream)
        stream.expect(",")
        item.identifier = parse_number(stream)
        stream.expect(",")
        item.class_name = parse_string(stream) if stream.peek().kind == "string" else parse_number(stream)
        stream.expect(",")
        item.style = parse_style(stream, 0x50000000)
        stream.expect(",")
    elif word in CONTROL_DEFAULTS:
        base, klass, extra = CONTROL_DEFAULTS[word]
        item.class_name = klass
        item.style = base | extra

        if word not in TEXTLESS_CONTROLS:
            item.text = parse_string(stream) if stream.peek().kind == "string" else parse_number(stream)
            stream.expect(",")

        item.identifier = parse_number(stream)
        stream.expect(",")
    else:
        raise Error("line %d: %r is not a control statement" % (token.line, token.text))

    item.x = parse_number(stream)
    stream.expect(",")
    item.y = parse_number(stream)

    trailing = []
    while stream.peek() is not None and stream.peek().text == ",":
        stream.next_token()
        if word == "CONTROL" and len(trailing) >= 2:
            trailing.append(parse_style(stream, 0))
        elif word != "CONTROL" and len(trailing) >= 2:
            trailing.append(parse_style(stream, item.style if len(trailing) == 2 else 0))
        else:
            trailing.append(parse_number(stream))

    if trailing:
        item.cx = trailing[0]
    if len(trailing) > 1:
        item.cy = trailing[1]

    if word == "CONTROL":
        if len(trailing) > 2:
            item.ex_style = trailing[2]
        if len(trailing) > 3:
            item.help_id = trailing[3]
    else:
        if len(trailing) > 2:
            item.style = trailing[2]
        if len(trailing) > 3:
            item.ex_style = trailing[3]
        if len(trailing) > 4:
            item.help_id = trailing[4]

    return item


# ---------------------------------------------------------------------------
# Binary encoding
# ---------------------------------------------------------------------------
def wide(text):
    return text.encode("utf-16-le") + b"\x00\x00"


def name_field(value):
    """Writes a template's name field: nothing, an ordinal, or text."""
    if value is None or value == "":
        return b"\x00\x00"
    if isinstance(value, int):
        return struct.pack("<HH", 0xFFFF, value & 0xFFFF)
    return wide(value)


def class_field(value):
    if isinstance(value, str):
        ordinal = STOCK_CLASSES.get(value.lower())
        if ordinal is not None:
            return struct.pack("<HH", 0xFFFF, ordinal)
    return name_field(value)


def align_to(block, boundary=4):
    while len(block) % boundary:
        block += b"\x00"
    return block


def build_dialog(dialog):
    out = b""

    if dialog.extended:
        out += struct.pack("<HHIII", 1, 0xFFFF, dialog.help_id, dialog.ex_style, dialog.style)
    else:
        out += struct.pack("<II", dialog.style, dialog.ex_style)

    out += struct.pack("<Hhhhh", len(dialog.items), dialog.x, dialog.y, dialog.cx, dialog.cy)
    out += name_field(dialog.menu)
    out += class_field(dialog.class_name)
    out += wide(dialog.caption) if dialog.caption else b"\x00\x00"

    if dialog.font is not None:
        size, face, weight, italic, charset = dialog.font
        if dialog.extended:
            out += struct.pack("<HHBB", size, weight, italic, charset)
        else:
            out += struct.pack("<H", size)
        out += wide(face)

    for item in dialog.items:
        out = align_to(out)

        if dialog.extended:
            out += struct.pack("<III", item.help_id, item.ex_style, item.style)
        else:
            out += struct.pack("<II", item.style, item.ex_style)

        out += struct.pack("<hhhh", item.x, item.y, item.cx, item.cy)
        out += struct.pack("<I", item.identifier) if dialog.extended else struct.pack("<H", item.identifier & 0xFFFF)
        out += class_field(item.class_name)
        out += name_field(item.text)
        out += struct.pack("<H", 0)

    return out


def build_string_bundle(strings, base):
    out = b""

    for index in range(16):
        text = strings.get(base + index, "")
        out += struct.pack("<H", len(text)) + text.encode("utf-16-le")

    return out


def build_version_node(key, value, children, binary=False):
    """Assembles one node of a version resource, padding as the format requires."""
    body = struct.pack("<HHH", 0, 0, 0 if binary else 1) + wide(key)
    body = align_to(body)

    if binary:
        words = len(value)
        body += value
    else:
        words = (len(value) + 1) if value is not None else 0
        body += wide(value) if value is not None else b""

    for child in children:
        body = align_to(body)
        body += child

    body = struct.pack("<HHH", len(body), words, 0 if binary else 1) + body[6:]
    return body


class Directory:
    """One level of the resource tree, keyed the way the directory itself is."""

    def __init__(self):
        self.entries = {}

    def add(self, key, value):
        self.entries[key] = value


def build_directory(tree):
    """Lays out a resource directory, addressing every leaf from its own start."""
    levels = []

    def gather(node, level):
        while len(levels) <= level:
            levels.append([])
        levels[level].append(node)
        for key in sorted(node.entries):
            child = node.entries[key]
            if isinstance(child, Directory):
                gather(child, level + 1)

    gather(tree, 0)

    offsets = {}
    point = 0

    for level in levels:
        for node in level:
            offsets[id(node)] = point
            point += 16 + 8 * len(node.entries)

    leaves = []
    for level in levels:
        for node in level:
            for key in sorted(node.entries):
                child = node.entries[key]
                if not isinstance(child, Directory):
                    leaves.append((node, key, child))

    leafoffsets = {}
    for node, key, data in leaves:
        leafoffsets[(id(node), key)] = point
        point += 16

    point = (point + 3) & ~3
    dataoffsets = {}
    for node, key, data in leaves:
        dataoffsets[(id(node), key)] = point
        point += (len(data) + 3) & ~3

    image = bytearray(point)

    for level in levels:
        for node in level:
            base = offsets[id(node)]
            struct.pack_into("<IIHHHH", image, base, 0, 0, 0, 0, 0, len(node.entries))
            slot = base + 16
            for key in sorted(node.entries):
                child = node.entries[key]
                if isinstance(child, Directory):
                    struct.pack_into("<II", image, slot, key, offsets[id(child)] | 0x80000000)
                else:
                    struct.pack_into("<II", image, slot, key, leafoffsets[(id(node), key)])
                slot += 8

    for node, key, data in leaves:
        entry = leafoffsets[(id(node), key)]
        where = dataoffsets[(id(node), key)]
        struct.pack_into("<IIII", image, entry, where, len(data), 1252, 0)
        image[where:where + len(data)] = data

    return bytes(image)


# ---------------------------------------------------------------------------
# The script as a whole
# ---------------------------------------------------------------------------
class Script:
    def __init__(self):
        self.dialogs = {}
        self.strings = {}
        self.version = None
        self.language = 0x0409


def parse_script(tokens):
    script = Script()
    stream = Stream(tokens)

    while True:
        token = stream.peek()
        if token is None:
            break

        word = token.text.upper()

        if word == "LANGUAGE":
            stream.next_token()
            primary = parse_number(stream)
            stream.expect(",")
            sub = parse_number(stream)
            script.language = (sub << 10) | primary
            continue

        if word == "STRINGTABLE":
            stream.next_token()
            while stream.accept("DISCARDABLE") or stream.accept("LOADONCALL") \
                    or stream.accept("MOVEABLE") or stream.accept("PRELOAD") \
                    or stream.accept("FIXED"):
                pass
            stream.expect("BEGIN")
            while not stream.accept("END"):
                identifier = parse_number(stream)
                if stream.peek() is not None and stream.peek().text == ",":
                    stream.next_token()
                text = parse_string(stream)
                if identifier in script.strings:
                    raise Error("string %d is defined twice" % identifier)
                script.strings[identifier] = text
            continue

        name = stream.next_token()
        identifier = None
        if name.kind == "number":
            identifier = int(name.text.rstrip("uUlL"), 0)
        elif name.kind == "string":
            identifier = unescape(name.text)
        else:
            identifier = name.text

        kind = stream.next_token().text.upper()

        if kind in ("DIALOG", "DIALOGEX"):
            dialog = parse_dialog(stream, identifier, kind == "DIALOGEX")
            script.dialogs[identifier] = dialog
        elif kind == "VERSIONINFO":
            script.version = parse_version(stream)
        elif kind == "DLGINIT":
            skip_block(stream)
        else:
            raise Error("line %d: unsupported resource statement %r" % (name.line, kind))

    return script


def skip_block(stream):
    stream.expect("BEGIN")
    depth = 1

    while depth > 0:
        token = stream.next_token()
        if token.text.upper() == "BEGIN":
            depth += 1
        elif token.text.upper() == "END":
            depth -= 1


def parse_version(stream):
    """Reads a version resource into its fixed part and its blocks."""
    fixed = {"FILEVERSION": (0, 0, 0, 0), "PRODUCTVERSION": (0, 0, 0, 0),
             "FILEFLAGSMASK": 0, "FILEFLAGS": 0, "FILEOS": 0, "FILETYPE": 0,
             "FILESUBTYPE": 0}

    while True:
        token = stream.peek()
        if token is None:
            raise Error("the version resource runs off the end of the script")

        word = token.text.upper()

        if word == "BEGIN":
            stream.next_token()
            break
        if word in ("FILEVERSION", "PRODUCTVERSION"):
            stream.next_token()
            parts = [parse_number(stream)]
            while stream.peek() is not None and stream.peek().text == ",":
                stream.next_token()
                parts.append(parse_number(stream))
            while len(parts) < 4:
                parts.append(0)
            fixed[word] = tuple(parts[:4])
        elif word in fixed:
            stream.next_token()
            fixed[word] = parse_style(stream, 0)
        else:
            raise Error("line %d: unexpected %r in a version resource" % (token.line, token.text))

    return (fixed, parse_version_block(stream))


def parse_version_block(stream):
    """Reads the body of a version block up to its END."""
    entries = []

    while not stream.accept("END"):
        token = stream.next_token()
        word = token.text.upper()

        if word == "BLOCK":
            name = parse_string(stream)
            stream.expect("BEGIN")
            entries.append(("block", name, parse_version_block(stream)))
        elif word == "VALUE":
            name = parse_string(stream)
            values = []
            while stream.peek() is not None and stream.peek().text == ",":
                stream.next_token()
                if stream.peek().kind == "string":
                    values.append(parse_string(stream))
                else:
                    values.append(parse_number(stream))
            entries.append(("value", name, values))
        else:
            raise Error("line %d: unexpected %r in a version block" % (token.line, token.text))

    return entries


def build_version_resource(version):
    fixed, blocks = version

    signature = struct.pack("<IIIIIIIIIIIIII",
        0xFEEF04BD, 0x00010000,
        (fixed["FILEVERSION"][0] << 16) | fixed["FILEVERSION"][1],
        (fixed["FILEVERSION"][2] << 16) | fixed["FILEVERSION"][3],
        (fixed["PRODUCTVERSION"][0] << 16) | fixed["PRODUCTVERSION"][1],
        (fixed["PRODUCTVERSION"][2] << 16) | fixed["PRODUCTVERSION"][3],
        fixed["FILEFLAGSMASK"], fixed["FILEFLAGS"], fixed["FILEOS"],
        fixed["FILETYPE"], fixed["FILESUBTYPE"], 0, 0, 0)[:52]

    def build(entries):
        out = []
        for kind, name, body in entries:
            if kind == "block":
                out.append(build_version_node(name, None, build(body)))
            else:
                if body and isinstance(body[0], str):
                    out.append(build_version_node(name, "".join(body), []))
                else:
                    packed = b"".join(struct.pack("<H", v & 0xFFFF) for v in body)
                    out.append(build_version_node(name, packed, [], binary=True))
        return out

    return build_version_node("VS_VERSION_INFO", signature, build(blocks), binary=True)


def build_image(script):
    types = Directory()

    dialogs = Directory()
    for identifier, dialog in script.dialogs.items():
        if not isinstance(identifier, int):
            raise Error("dialog %r is not numbered" % identifier)
        language = Directory()
        language.add(script.language, build_dialog(dialog))
        dialogs.add(identifier, language)

    strings = Directory()
    bundles = {}
    for identifier in script.strings:
        bundles.setdefault(identifier // 16, {})[identifier] = script.strings[identifier]
    for bundle, members in bundles.items():
        language = Directory()
        language.add(script.language, build_string_bundle(members, bundle * 16))
        strings.add(bundle + 1, language)

    if dialogs.entries:
        types.add(RESOURCE_TYPE_DIALOG, dialogs)
    if strings.entries:
        types.add(RESOURCE_TYPE_STRING, strings)

    if script.version is not None:
        version = Directory()
        language = Directory()
        language.add(script.language, build_version_resource(script.version))
        version.add(1, language)
        types.add(RESOURCE_TYPE_VERSION, version)

    return build_directory(types)


BANNER = """\
/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

// Generated from code/language/language.rc by code/language/rcimage.py. Do not edit.

#include "language/languageimage.h"


unsigned char const LanguageResourceImage[%d] = {
"""


def write_source(path, image):
    lines = []
    for start in range(0, len(image), 16):
        chunk = image[start:start + 16]
        lines.append("\t" + " ".join("0x%02X," % byte for byte in chunk))

    with open(path, "w", newline="\n") as handle:
        handle.write(BANNER % len(image))
        handle.write("\n".join(lines))
        handle.write("\n};\n\n\nunsigned int const LanguageResourceImageSize = %d;\n" % len(image))


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("script")
    parser.add_argument("--include", action="append", default=[],
        help="a directory the script's includes are looked for in")
    parser.add_argument("--source", help="the C++ source to write the image to")
    parser.add_argument("--binary", help="the file to write the bare image to")
    arguments = parser.parse_args(argv[1:])

    defines = {name: tokenize(str(value)) for name, value in WIN32_DEFINES.items()}
    defines["_WIN32"] = tokenize("1")

    processor = Preprocessor(arguments.include, defines)
    processor.process(arguments.script)

    script = parse_script(expand([t for t in processor.output if t.kind != "newline"], processor.defines))
    image = build_image(script)

    if arguments.binary:
        with open(arguments.binary, "wb") as handle:
            handle.write(image)

    if arguments.source:
        write_source(arguments.source, image)

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except Error as problem:
        sys.stderr.write("rcimage: %s\n" % problem)
        sys.exit(1)
