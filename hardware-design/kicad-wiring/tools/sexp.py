import re
def extract(text, name):
    """Return the balanced '(symbol "name" ...)' block at top level of a .kicad_sym."""
    key = f'(symbol "{name}"'
    i = text.find(key)
    if i < 0: raise KeyError(name)
    d = 0; j = i
    while True:
        c = text[j]
        if c == '"':
            j += 1
            while text[j] != '"':
                j += 2 if text[j] == '\\' else 1
        elif c == '(': d += 1
        elif c == ')':
            d -= 1
            if d == 0: return text[i:j+1]
        j += 1
PIN = re.compile(r'\(pin (\w+) (\w+)\s*\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)\s*\(length ([-\d.]+)\)(?:\s*hide)?\s*\(name "([^"]*)".*?\(number "([^"]*)"', re.S)
def pins(block):
    return [dict(type=m[0], x=float(m[2]), y=float(m[3]), ang=float(m[4]), name=m[6], num=m[7]) for m in PIN.findall(block)]
